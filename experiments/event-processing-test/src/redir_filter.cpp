#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <linux/filter.h>
#include <linux/limits.h>
#include <linux/seccomp.h>
#include <linux/unistd.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include "filter.h"

using namespace Filter;

int main(int argc, char **argv) {
  pid_t pid;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <prog> <arg1> ... <argN>\n", argv[0]);
    return 1;
  }

  if ((pid = fork()) == 0) {
    /* If open syscall, trace */

    int n_syscalls =
        (sizeof(syscalls_intercept) / sizeof(syscalls_intercept[0]));
    int filter_len = n_syscalls + 3;

    struct sock_filter filter[filter_len];
    filter[0] =
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, offsetof(struct seccomp_data, nr));
    filter[filter_len - 2] = BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW);
    filter[filter_len - 1] = BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_TRACE);

    for (int i = 0; i < n_syscalls; i++) {
      filter[i + 1] = BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, syscalls_intercept[i],
                               u_int8_t(n_syscalls - i), 0);
    }

    struct sock_fprog prog = {
        (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        filter,
    };
    ptrace(PTRACE_TRACEME, 0, 0, 0);
    /* To avoid the need for CAP_SYS_ADMIN */
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1) {
      perror("prctl(PR_SET_NO_NEW_PRIVS)");
      return 1;
    }
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) == -1) {
      perror("when setting seccomp filter");
      return 1;
    }
    kill(getpid(), SIGSTOP);
    return execvp(argv[1], argv + 1);
  } else {
    Manager manager(pid);
    while (manager.to_next_event())
      ;
    return 0;
  }
}