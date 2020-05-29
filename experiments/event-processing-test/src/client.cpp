#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dirent.h"
#include "time.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <fstream>
#include <linux/filter.h>
#include <linux/limits.h>
#include <linux/seccomp.h>
#include <linux/unistd.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <queue>
#include <sstream>
#include <string>
#include <vector>

#include "client.h"

namespace {
void _read_from_proc(pid_t child, char *out, char *addr, size_t len) {
  size_t i = 0;
  while (i < len) {
    long val;

    val = ptrace(PTRACE_PEEKTEXT, child, addr + i, NULL);
    if (val == -1) {
      fprintf(stderr, "failed to PEEKTEXT\n");
      exit(1);
    }

    memcpy(out + i, &val, sizeof(long));
    i += sizeof(long);
  }
}

void _write_to_proc(pid_t child, char *to_write, char *addr, size_t len) {
  size_t i = 0;
  while (i < len) {
    char val[sizeof(long)];
    memcpy(val, to_write + i, sizeof(long));

    long ret = ptrace(PTRACE_POKETEXT, child, addr + i, *(long *)val);
    if (ret == -1) {
      exit(1);
    }

    i += sizeof(long);
  }
}

} // namespace

namespace ClientFilter {

ClientManager::ClientManager(int client_idx, std::string seed,
                             std::vector<std::string> command, FdMap &fdmap,
                             bool ignore_stdout)
    : my_idx(client_idx), seed(seed), command(command),
      ignore_stdout(ignore_stdout), fdmap(fdmap), sockfds() {
  printf("[CLIENT] creating with command: %s\n", command[0].c_str());

  start_client();
}

void ClientManager::toggle_client() {
  if (child > 0) {
    stop_client();
  } else {
    start_client();
  }
}

void ClientManager::start_client() {
  pid_t pid;
  if ((pid = fork()) == 0) {
    /* If open syscall, trace */
    if (ignore_stdout) {
      int devNull = open("/dev/null", O_WRONLY);
      dup2(devNull, STDOUT_FILENO);
    } else {
      std::ostringstream oss;
      oss << "/tmp/client_";
      oss << seed;
      oss << "_";
      oss << my_idx;
      std::string client_out = oss.str();
      int file = open(client_out.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
      dup2(file, STDOUT_FILENO);
    }

    // set up seccomp for tracing syscalls
    // https://www.alfonsobeato.net/c/filter-and-modify-system-calls-with-seccomp-and-ptrace/
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
      exit(1);
    }
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) == -1) {
      perror("when setting seccomp filter");
      exit(1);
    }
    kill(getpid(), SIGSTOP);
    const char **args = new const char *[command.size() + 2];
    for (size_t i = 0; i < command.size(); i++) {
      args[i] = command[i].c_str();
    }
    args[command.size()] = NULL;
    printf("Executing %d\n", getpid());
    exit(execv(args[0], (char **)args));
  }

  child = pid;

  int status;
  waitpid(pid, &status, 0);
  ptrace(PTRACE_SETOPTIONS, pid, 0,
         PTRACE_O_TRACESECCOMP | PTRACE_O_TRACESYSGOOD);

  child_state = ST_STOPPED;
}

void ClientManager::stop_client() {
  kill(child, SIGKILL);
  int status;
  while (waitpid(child, &status, 0) != child)
    ;
  child = -1;
  child_state = ST_DEAD;

  sockfds.clear();
  fdmap.clear_nodefds(my_idx);
}

Event ClientManager::to_next_event() {
  printf("[CLIENT] handling to_next_event\n");
  if (child_state == ST_DEAD)
    return EV_DEAD;
  else if (child_state == ST_RECVING)
    handle_recv();
  else if (child_state != ST_STOPPED) {
    fprintf(stderr, "[CLIENT] child in unexpected state\n");
    exit(1);
  }

  int status;
  while (1) {
    ptrace(PTRACE_CONT, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status)) {
      if (status >> 8 == (SIGTRAP | (PTRACE_EVENT_SECCOMP << 8))) {
        // check if it's one of the syscalls we want to handle
        int my_syscall =
            ptrace(PTRACE_PEEKUSER, child, sizeof(long) * ORIG_RAX, 0);
        if (my_syscall > 0) {
          // If attempting to read the data failed, that may be because
          // program's still running
          child_state = ST_STOPPED;
        }
        switch (my_syscall) {
          // network-related
        case SYS_socket:
          handle_socket();
          continue;
        case SYS_shutdown: // we aren't *really* accounting for shutdown
        case SYS_close: {
          struct user_regs_struct regs;
          ptrace(PTRACE_GETREGS, child, 0, &regs);
          int fd = regs.rdi;

          if (sockfds.find(fd) != sockfds.end()) {
            child_state = ST_NETWORK;
            return EV_CLOSE;
          } else {
            child_state = ST_STOPPED;
            continue;
          }
        }
        case SYS_connect:
          printf("[CLIENT] about to handle connect\n");
          child_state = ST_NETWORK;
          return EV_CONNECT;
        case SYS_sendto:
          printf("[CLIENT] about to handle sendto\n");
          child_state = ST_NETWORK;
          return EV_SENDTO;
        case SYS_recvfrom:
          printf("[CLIENT] waiting to recv\n");
          child_state = ST_RECVING;
          return EV_RECVING;
        default:
          continue;
        }
      } else {
        child_state = ST_STOPPED;
      }
    } else if (WIFEXITED(status)) {
      printf("[CLIENT] exited\n");
      child_state = ST_DEAD;
      return EV_EXIT;
    } else {
      fprintf(stderr, "[CLIENT] unexpected waitpid status\n");
      exit(1);
    }
  }
}

int ClientManager::allow_event(Event ev) {
  child_state = ST_STOPPED;
  switch (ev) {
  case EV_CONNECT: {
    int status;
    ptrace(PTRACE_SYSCALL, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
      struct user_regs_struct regs;
      ptrace(PTRACE_GETREGS, child, 0, &regs);
      int ret = (int)regs.rax;
      int connfd = (int)regs.rdi;
      if (ret < 0) {
        printf("[CLIENT] connect failed with %s\n", strerror(-ret));
        deadfds.insert(connfd);
        return -1;
      } else {
        fdmap.node_connect_fd(my_idx, connfd);
        return 0;
      }
    }
    return 0;
  }
  case EV_SENDTO: {
    int status;
    ptrace(PTRACE_SYSCALL, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
      struct user_regs_struct regs;
      ptrace(PTRACE_GETREGS, child, 0, &regs);
      int sendfd = (int)regs.rdi;
      if (fdmap.is_nodefd_alive(my_idx, sendfd)) {
        printf("[CLIENT] connection is still alive. allow sendto\n");
        return (int)regs.rax;
      } else {
        // proxy already closed. we should close this fd
        printf("[CLIENT] connection is dead. inject failure\n");
        regs.rax = -((long)ECONNREFUSED);
        ptrace(PTRACE_SETREGS, child, 0, &regs);
        return -1;
      }
    }
    fprintf(stderr, "[CLIENT] did not get subsequent syscall\n");
    exit(1);
    return -1;
  }
  default:
    fprintf(stderr, "[CLIENT] invalid event to allow\n");
    exit(1);
  }
  return -1;
}

bool ClientManager::handle_close() {
  int status;
  ptrace(PTRACE_SYSCALL, child, 0, 0);
  waitpid(child, &status, 0);
  child_state = ST_STOPPED;

  if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
    int fd = (int)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RDI, 0);
    printf("[CLIENT] closed sockfd: %d\n", fd);
    sockfds.erase(fd);
    bool no_conn_fail = (deadfds.find(fd) == deadfds.end());
    deadfds.erase(fd);
    return no_conn_fail && fdmap.is_nodefd_alive(my_idx, fd);
  }
  return false;
}

void ClientManager::handle_recv() {
  printf("[CLIENT] handling recv\n");
  int status;
  struct user_regs_struct regs;
  ptrace(PTRACE_GETREGS, child, 0, &regs);
  printf("[CLIENT] recving on fd %d\n", (int)regs.rdi);

  fflush(stdout);
  int sendfd = (int)regs.rdi;
  if (fdmap.is_nodefd_alive(my_idx, sendfd)) {
    printf("[CLIENT] connection is still alive. allow recv\n");
  } else {
    // proxy already closed. we should close this fd
    printf("[CLIENT] connection is dead. inject failure\n");
    regs.orig_rax = -1;
    ptrace(PTRACE_SYSCALL, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
      ptrace(PTRACE_GETREGS, child, 0, &regs);
      regs.rax = -((long)ETIMEDOUT);
    } else {
      fprintf(stderr, "[CLIENT] did not get subsequent syscall\n");
      exit(1);
    }
  }
  fflush(stdout);
}

void ClientManager::handle_socket() {
  printf("[CLIENT] handling socket\n");

  struct user_regs_struct regs;
  ptrace(PTRACE_GETREGS, child, 0, &regs);
  int domain = regs.rdi;
  int type = regs.rsi;
  // int protocol = regs.rdx;

  // printf("domain: %d, type: %d, protocol: %d\n", domain, type, protocol);

  if (domain == AF_INET && type & SOCK_STREAM) {
    // when TCP, track the socket being returned
    int status;
    ptrace(PTRACE_SYSCALL, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
      uint64_t fd =
          (uint64_t)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RAX, 0);
      printf("[CLIENT] new sockfd: %lu\n", fd);
      sockfds.insert(fd);
    }
  }
}

} // namespace ClientFilter