#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "filter.h"

namespace {
int _get_offs_for_arg(int arg) {
  switch (arg) {
  case 1:
    return RDI;
  case 2:
    return RSI;
  default:
    fprintf(stderr, "no support for arguments greater than two yet");
    exit(1);
  }
}

// assumes file is 2nd argument
void _read_file(pid_t child, char *file, int arg) {
  char *child_addr;
  size_t i;

  int offs = _get_offs_for_arg(arg);
  child_addr = (char *)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * offs, 0);

  do {
    long val;
    char *p;

    val = ptrace(PTRACE_PEEKTEXT, child, child_addr, NULL);
    if (val == -1) {
      fprintf(stderr, "PTRACE_PEEKTEXT error: %s", strerror(errno));
      exit(1);
    }
    child_addr += sizeof(long);

    p = (char *)&val;
    for (i = 0; i < sizeof(long); ++i, ++file) {
      *file = *p++;
      if (*file == '\0')
        break;
    }
  } while (i == sizeof(long));
}

void _redirect_file(pid_t child, const char *file, int arg) {
  char *stack_addr, *file_addr;
  int offs = _get_offs_for_arg(arg);

  stack_addr = (char *)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RSP, 0);
  /* Move further of red zone and make sure we have space for the file name */
  stack_addr -= 128 + PATH_MAX;
  file_addr = stack_addr;

  /* Write new file in lower part of the stack */
  do {
    size_t i;
    char val[sizeof(long)];

    for (i = 0; i < sizeof(long); ++i, ++file) {
      val[i] = *file;
      if (*file == '\0')
        break;
    }

    ptrace(PTRACE_POKETEXT, child, stack_addr, *(long *)val);
    stack_addr += sizeof(long);
  } while (*file);

  /* Change argument to open */
  ptrace(PTRACE_POKEUSER, child, sizeof(long) * offs, file_addr);
}

void _read_from_proc(pid_t child, char *out, char *addr, size_t len) {
  size_t i = 0;
  while (i < len) {
    long val;

    val = ptrace(PTRACE_PEEKTEXT, child, addr + i, NULL);
    if (val == -1) {
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

namespace Filter {

Manager::Manager(std::vector<std::string> command, sockaddr_in old_addr,
                 sockaddr_in new_addr)
    : command(command), old_addr(old_addr), new_addr(new_addr), fds(),
      sockfds() {
  printf("[FILTER] creating with command: %s %s\n", command[0].c_str(),
         command[1].c_str());

  start_node();
}

void Manager::toggle_node() {
  if (child > 0) {
    stop_node();
  } else {
    start_node();
  }
}

void Manager::start_node() {
  pid_t pid;
  if ((pid = fork()) == 0) {
    /* If open syscall, trace */
    // int devNull = open("/dev/null", O_WRONLY);
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
    printf("Executing %d with %s\n", getpid(), args[1]);
    exit(execv(args[0], (char **)args));
  }

  child = pid;

  int status;
  waitpid(pid, &status, 0);
  ptrace(PTRACE_SETOPTIONS, pid, 0,
         PTRACE_O_TRACESECCOMP | PTRACE_O_TRACESYSGOOD);

  send_continue = true;
}

void Manager::stop_node() {
  kill(child, SIGKILL);
  int status;
  while (waitpid(child, &status, 0) != child)
    ;
  child = -1;
  send_continue = false;

  fds.clear();
  sockfds.clear();
}

Event Manager::to_next_event() {
  if (child < 0)
    return EV_DEAD;

  int status;
  int counter = 0;
  while (1) {
    if (counter > 2) {
      return send_continue ? EV_NONE : EV_RUNNING;
    }
    if (send_continue) {
      ptrace(PTRACE_CONT, child, 0, 0);
      send_continue = false;
    }
    // TODO - figure out how to do this so we don't get stuck waiting for an
    // epoll_wait or something else blocking
    waitpid(child, &status, WNOHANG);
    counter++;
    if (WIFSTOPPED(status)) {
      if (status >> 8 == (SIGTRAP | (PTRACE_EVENT_SECCOMP << 8))) {
        // check if it's one of the syscalls we want to handle
        int my_syscall =
            ptrace(PTRACE_PEEKUSER, child, sizeof(long) * ORIG_RAX, 0);
        if (my_syscall > 0) {
          // If attempting to read the data failed, that may be because
          // program's still running
          send_continue = true;
        }
        switch (my_syscall) {
        case SYS_openat:
          handle_open(true);
          continue;
        case SYS_open:
          handle_open(false);
          continue;
        case SYS_stat:
          handle_stat();
          continue;
        case SYS_mknod:
          handle_mknod();
          continue;
        case SYS_fdatasync:
        case SYS_fsync:
          handle_fsync();
          continue;

        case SYS_socket:
          handle_socket();
          continue;
        case SYS_bind:
          handle_bind();
          continue;
        case SYS_getsockname:
          handle_getsockname();
          continue;
        // case SYS_connect:
        //   handle_connect();
        //   continue;
        // case SYS_accept:
        // case SYS_accept4:
        //   handle_accept();
        //   continue;
        case SYS_sendto:
          // handle_send();
          printf("handling sendto\n");
          return EV_SENDTO;
        // case SYS_recvfrom:
        //   handle_recv();
        //   continue;
        // case SYS_read:
        //   handle_read();
        //   continue;
        default:
          continue;
        }
      } else {
        send_continue = true;
      }
      if (WIFEXITED(status)) {
        printf("[FILTER] exited\n");
        return EV_EXIT;
      }
    }
  }
}

void Manager::handle_open(bool at) {
  printf("handling open with %s\n", at ? "at" : "no at");
  char orig_file[PATH_MAX];
  char new_file[PATH_MAX];

  int arg = at ? 2 : 1;
  _read_file(child, orig_file, arg);
  printf("old: %s\n", orig_file);

  if (strncmp(prefix, orig_file, strlen(prefix)) == 0) {
    if (strlen(orig_file) + strlen(suffix) + 10 < PATH_MAX) {
      strcpy(new_file, orig_file);
      strcpy(new_file + strlen(orig_file), suffix);
      new_file[strlen(orig_file) + strlen(suffix) + 1] = 0;
    } else {
      fprintf(stderr, "too big of an argument rip\n");
      exit(1);
    }
    printf("to write: %s\n", new_file);

    _redirect_file(child, new_file, arg);
    _read_file(child, new_file, arg);
    printf("new: %s\n", new_file);

    // get file descriptor for the opened file so we can register it
    int status;
    ptrace(PTRACE_SYSCALL, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
      uint64_t fd =
          (uint64_t)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RAX, 0);
      printf("return val: %lu\n", fd);
      fds[fd] = std::string(orig_file);
    }
  }
}

void Manager::handle_stat() {
  printf("handling stat\n");
  char orig_file[PATH_MAX];
  char new_file[PATH_MAX];

  _read_file(child, orig_file, 1);
  printf("old: %s\n", orig_file);

  if (strncmp(prefix, orig_file, strlen(prefix)) == 0) {
    if (strlen(orig_file) + strlen(suffix) + 10 < PATH_MAX) {
      strcpy(new_file, orig_file);
      strcpy(new_file + strlen(orig_file), suffix);
      new_file[strlen(orig_file) + strlen(suffix) + 1] = 0;
    } else {
      fprintf(stderr, "too big of an argument rip\n");
      exit(1);
    }
    printf("to write: %s\n", new_file);

    _redirect_file(child, new_file, 1);
    _read_file(child, new_file, 1);
    printf("new: %s\n", new_file);
  }
}

void Manager::handle_mknod() {
  printf("handling mknod\n");
  char orig_file[PATH_MAX];
  char new_file[PATH_MAX];

  _read_file(child, orig_file, 1);
  printf("old: %s\n", orig_file);

  if (strncmp(prefix, orig_file, strlen(prefix)) == 0) {
    if (strlen(orig_file) + strlen(suffix) + 10 < PATH_MAX) {
      strcpy(new_file, orig_file);
      strcpy(new_file + strlen(orig_file), suffix);
      new_file[strlen(orig_file) + strlen(suffix) + 1] = 0;
    } else {
      fprintf(stderr, "too big of an argument rip\n");
      exit(1);
    }
    printf("to write: %s\n", new_file);

    _redirect_file(child, new_file, 1);
    _read_file(child, new_file, 1);
    printf("new: %s\n", new_file);
  }
}

void Manager::handle_fsync() {
  printf("handling fsync\n");

  int fd = (int)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RDI, 0);
  printf("fd: %d\n", fd);

  auto it = fds.find(fd);
  if (it != fds.end()) {
    auto orig_file = it->second;
    if (orig_file[0] != '/') {
      fprintf(stderr, "relative path rip\n");
      exit(1);
    }

    std::string back_file;
    back_file = orig_file;
    back_file.append(suffix);
    printf("Copying %s to %s\n", back_file.c_str(), orig_file.c_str());
    std::ifstream src(back_file, std::ios::binary);
    std::ofstream dst(orig_file, std::ios::binary);
    dst << src.rdbuf();
  }
}

void Manager::handle_socket() {
  printf("handling socket\n");

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
      printf("sockfd: %lu\n", fd);
      sockfds[fd] = false;
    }
  }
}

void Manager::handle_bind() {
  printf("[FILTER] handling bind\n");

  struct user_regs_struct regs;
  ptrace(PTRACE_GETREGS, child, 0, &regs);
  int sockfd = regs.rdi;
  unsigned long long sockaddr_ptr = regs.rsi;
  size_t addrlen = regs.rdx;

  sockaddr_in curr_addr;
  _read_from_proc(child, (char *)&curr_addr, (char *)sockaddr_ptr, addrlen);

  printf("[FILTER] sockfd: %d oldaddr: %d %s:%d\n", sockfd,
         curr_addr.sin_family, inet_ntoa(curr_addr.sin_addr),
         ntohs(curr_addr.sin_port));

  if (curr_addr.sin_family == AF_INET &&
      curr_addr.sin_addr.s_addr == old_addr.sin_addr.s_addr) {
    // overwrite the address, in the same location
    // NOTE - we're assuming that they're the same width
    sockaddr_in my_new_addr;
    my_new_addr.sin_addr.s_addr = new_addr.sin_addr.s_addr;
    my_new_addr.sin_port = curr_addr.sin_port;
    my_new_addr.sin_family = AF_INET;
    printf("[FILTER] writing my_new_addr %s:%d to proc for %d\n",
           inet_ntoa(my_new_addr.sin_addr), ntohs(my_new_addr.sin_port),
           sockfd);
    _write_to_proc(child, (char *)&my_new_addr, (char *)sockaddr_ptr,
                   sizeof(sockaddr_in));
    sockfds[sockfd] = true;

    // overwrite the arguments back after the syscall
    int status;
    ptrace(PTRACE_SYSCALL, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
      printf("[FILTER] overwriting %s:%d to proc\n",
             inet_ntoa(curr_addr.sin_addr), ntohs(curr_addr.sin_port));
      _write_to_proc(child, (char *)&curr_addr, (char *)sockaddr_ptr,
                     sizeof(sockaddr_in));
    }
  }
}

void Manager::handle_getsockname() {
  printf("[FILTER] handling getsockname\n");
  struct user_regs_struct regs;
  ptrace(PTRACE_GETREGS, child, 0, &regs);
  int sockfd = regs.rdi;
  unsigned long long sockaddr_ptr = regs.rsi;
  unsigned long long addrlen_ptr = regs.rdx;

  auto got = sockfds.find(sockfd);
  if (got == sockfds.end() || !got->second) {
    // not redirected, just ignore
    printf("[FILTER] sockfd %d not redirectd, ignore\n", sockfd);
    return;
  }

  long addrlen_lng;
  _read_from_proc(child, (char *)&addrlen_lng, (char *)addrlen_ptr,
                  sizeof(long));
  size_t addrlen = (size_t)addrlen_lng;
  if (addrlen < sizeof(sockaddr_in)) {
    fprintf(stderr, "Found addrlen that's smaller than expected.");
    exit(1);
  }

  int status;
  ptrace(PTRACE_SYSCALL, child, 0, 0);
  waitpid(child, &status, 0);
  if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
    printf("[FILTER] length of overwrite: %ld\n", addrlen);
    sockaddr_in addr_to_overwrite;
    _read_from_proc(child, (char *)&addr_to_overwrite, (char *)sockaddr_ptr,
                    sizeof(sockaddr_in));
    addr_to_overwrite.sin_addr.s_addr = old_addr.sin_addr.s_addr;
    printf("[FILTER] overwriting %s:%d to proc\n",
           inet_ntoa(addr_to_overwrite.sin_addr),
           ntohs(addr_to_overwrite.sin_port));
    _write_to_proc(child, (char *)&addr_to_overwrite, (char *)sockaddr_ptr,
                   sizeof(sockaddr_in));
  }
}

} // namespace Filter