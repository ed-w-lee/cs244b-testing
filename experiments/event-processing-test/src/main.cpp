#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/limits.h>
#include <linux/seccomp.h>
#include <linux/unistd.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <vector>

#include "filter.h"
#include "proxy.h"

using namespace Filter;

int main(int argc, char **argv) {
  pid_t pid;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <prog> <arg1> ... <argN>\n", argv[0]);
    return 1;
  }

  const int NUM_NODES = 3;
  sockaddr_in oldaddrs[NUM_NODES];
  for (int i = 0; i < NUM_NODES; i++) {
    oldaddrs[i].sin_family = AF_INET;
    oldaddrs[i].sin_port = htons(4242);
  }
  oldaddrs[0].sin_addr.s_addr = inet_addr("127.0.0.11");
  oldaddrs[1].sin_addr.s_addr = inet_addr("127.0.0.21");
  oldaddrs[2].sin_addr.s_addr = inet_addr("127.0.0.31");

  sockaddr_in newaddrs[NUM_NODES];
  for (int i = 0; i < NUM_NODES; i++) {
    newaddrs[i].sin_family = AF_INET;
    newaddrs[i].sin_port = htons(4242);
  }
  newaddrs[0].sin_addr.s_addr = inet_addr("127.0.0.10");
  newaddrs[1].sin_addr.s_addr = inet_addr("127.0.0.20");
  newaddrs[2].sin_addr.s_addr = inet_addr("127.0.0.30");

  std::vector<Manager> managers;

  for (int i = 0; i < NUM_NODES; i++) {
    if ((pid = fork()) == 0) {
      /* If open syscall, trace */
      int devNull = open("/dev/null", O_WRONLY);
      // dup2(devNull, STDOUT_FILENO);

      int n_syscalls =
          (sizeof(syscalls_intercept) / sizeof(syscalls_intercept[0]));
      int filter_len = n_syscalls + 3;

      struct sock_filter filter[filter_len];
      filter[0] =
          BPF_STMT(BPF_LD + BPF_W + BPF_ABS, offsetof(struct seccomp_data, nr));
      filter[filter_len - 2] = BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW);
      filter[filter_len - 1] = BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_TRACE);

      for (int i = 0; i < n_syscalls; i++) {
        filter[i + 1] =
            BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, syscalls_intercept[i],
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
      char buf[10];
      snprintf(buf, 9, "%d", i);
      buf[9] = 0;
      printf("Executing %d with %s\n", getpid(), buf);
      return execl(argv[1], buf);
    }
    Manager manager(pid, oldaddrs[i], newaddrs[i]);
    managers.push_back(manager);
  }

  std::vector<sockaddr_in> newaddrs_vec;
  std::vector<sockaddr_in> oldaddrs_vec;
  for (int i = 0; i < NUM_NODES; i++) {
    newaddrs_vec.push_back(newaddrs[i]);
    oldaddrs_vec.push_back(oldaddrs[i]);
  }

  std::vector<Proxy> proxies;
  for (int i = 0; i < NUM_NODES; i++) {
    Proxy proxy(oldaddrs[i], newaddrs[i], newaddrs_vec, oldaddrs_vec);
    proxies.push_back(proxy);
  }

  while (true) {
    for (auto &proxy : proxies) {
      auto &q = proxy.get_msgs();
      for (auto &x : q) {
        if (x.second.size() > 0) {
          proxy.allow_next_msg(x.first);
        }
      }
    }
    for (auto &manager : managers) {
      manager.to_next_event();
    }
  }
}