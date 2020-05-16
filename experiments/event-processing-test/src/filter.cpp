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

namespace Filter {

const std::string Manager::suffix = ".__bk";

Manager::Manager(int my_idx, std::vector<std::string> command,
                 sockaddr_in old_addr, sockaddr_in new_addr, FdMap &fdmap,
                 std::string prefix, bool ignore_stdout)
    : my_idx(my_idx), command(command), ignore_stdout(ignore_stdout),
      fdmap(fdmap), prefix(prefix), old_addr(old_addr), new_addr(new_addr),
      fds(), sockfds() {
  printf("[FILTER] creating with command: %s %s\n", command[0].c_str(),
         command[1].c_str());

  vtime.tv_sec = 244244;
  vtime.tv_nsec = 244244244;

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
  restore_files();

  pid_t pid;
  if ((pid = fork()) == 0) {
    /* If open syscall, trace */
    if (ignore_stdout) {
      int devNull = open("/dev/null", O_WRONLY);
      dup2(devNull, STDOUT_FILENO);
    } else {
      char blah[200];
      strcpy(blah, "/tmp/filter_");
      strcat(blah, command[1].c_str());
      int file = open(blah, O_WRONLY | O_CREAT | O_APPEND, 0644);
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
    printf("Executing %d with %s\n", getpid(), args[1]);
    exit(execv(args[0], (char **)args));
  }

  child = pid;

  int status;
  waitpid(pid, &status, 0);
  ptrace(PTRACE_SETOPTIONS, pid, 0,
         PTRACE_O_TRACESECCOMP | PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEEXEC);

  ptrace(PTRACE_CONT, child, NULL, NULL);
  waitpid(pid, &status, 0);
  if (status >> 8 == (SIGTRAP | (PTRACE_EVENT_EXEC << 8))) {
    // disable vDSO to ensure we can intercept gettimeofday
    // reference: https://stackoverflow.com/a/52402306
    char todo[2000];
    long rsp = ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RSP, 0);
    size_t num_to_get = command.size() + 70;
    _read_from_proc(child, (char *)todo, (char *)rsp,
                    num_to_get * sizeof(long));
    int num_nulls = 0;
    int auxv_idx = 0;
    for (size_t i = 0; i < num_to_get; i++) {
      long ptr = *(long *)(&todo[i * sizeof(long)]);
      if (num_nulls == 2) {
        auxv_idx++;
        if (auxv_idx == 2) {
          // AT_SYSINFO_EHDR (vDSO location) experimentally found to be 2nd
          // element in aux vector (at least in my OS **thinking**)
          printf("[FILTER] overwriting vDSO: %lx\n", ptr);
          long null = 0;
          _write_to_proc(child, (char *)&null, (char *)rsp + (i * sizeof(long)),
                         sizeof(long));
        }
      }
      if (ptr == 0) {
        num_nulls++;
      }
    }
  }

  child_state = ST_STOPPED;
}

void Manager::stop_node() {
  kill(child, SIGKILL);
  int status;
  while (waitpid(child, &status, 0) != child)
    ;
  child = -1;
  child_state = ST_DEAD;

  for (auto &tup : fds) {
    backup_file(tup.second);
  }
  fds.clear();
  sockfds.clear();
  fdmap.clear_nodefds(my_idx);
}

Event Manager::to_next_event() {
  if (child_state == ST_DEAD)
    return EV_DEAD;
  else if (child_state == ST_POLLING)
    handle_poll();
  else if (child_state != ST_STOPPED) {
    fprintf(stderr, "[FILTER] child in unexpected state\n");
    exit(1);
  }

  int status;
  while (1) {
    ptrace(PTRACE_CONT, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status)) {
      if (status >> 8 == (SIGTRAP | (PTRACE_EVENT_SECCOMP << 8))) {
        increment_vtime(0, 1000);
        // check if it's one of the syscalls we want to handle
        int my_syscall =
            ptrace(PTRACE_PEEKUSER, child, sizeof(long) * ORIG_RAX, 0);
        if (my_syscall > 0) {
          // If attempting to read the data failed, that may be because
          // program's still running
          child_state = ST_STOPPED;
        }
        switch (my_syscall) {
          // polling-related
        case SYS_poll:
        case SYS_select:
          child_state = ST_POLLING;
          return EV_POLLING;
        case SYS_gettimeofday:
          handle_gettimeofday();
          continue;
        case SYS_clock_gettime:
          handle_clock_gettime();
          continue;

          // filesystem-related
        case SYS_openat:
          handle_open(true);
          continue;
        case SYS_open:
          handle_open(false);
          continue;
        case SYS_syncfs:
        case SYS_fdatasync:
        case SYS_fsync:
          printf("[FILTER] about to fsync\n");
          child_state = ST_WAITING_FSYNC;
          return EV_SYNCFS;

          // network-related
        case SYS_socket:
          handle_socket();
          continue;
        case SYS_bind:
          handle_bind();
          continue;
        case SYS_getsockname:
          handle_getsockname();
          continue;
        case SYS_accept4:
        case SYS_accept:
          handle_accept();
          continue;
        case SYS_connect:
          printf("[FILTER] about to handle connect\n");
          child_state = ST_NETWORK;
          return EV_CONNECT;
        case SYS_sendto:
          printf("[FILTER] about to handle sendto\n");
          child_state = ST_NETWORK;
          return EV_SENDTO;
        default:
          continue;
        }
      } else {
        child_state = ST_STOPPED;
      }
    } else if (WIFEXITED(status)) {
      printf("[FILTER] exited\n");
      child_state = ST_DEAD;
      return EV_EXIT;
    } else {
      fprintf(stderr, "[FILTER] unexpected waitpid status\n");
      exit(1);
    }
  }
}

int Manager::allow_event(Event ev) {
  child_state = ST_STOPPED;
  switch (ev) {
  case EV_SYNCFS: {
    handle_fsync();
    return 0;
  }
  case EV_CONNECT: {
    int status;
    ptrace(PTRACE_SYSCALL, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
      int ret = (int)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RAX, 0);
      if ((int)ret < 0) {
        return -1;
      } else {
        int connfd = (int)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RDI, 0);
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
        printf("[FILTER] connection is still alive. allow sendto\n");
        return (int)regs.rax;
      } else {
        // proxy already closed. we should close this fd
        printf("[FILTER] connection is dead. inject failure\n");
        regs.rax = -((long)ECONNRESET);
        ptrace(PTRACE_SETREGS, child, 0, &regs);
        return -1;
      }
    }
    fprintf(stderr, "[FILTER] did not get subsequent syscall\n");
    exit(1);
  }
  default:
    fprintf(stderr, "[FILTER] invalid event to allow\n");
    exit(1);
  }
  return -1;
}

void Manager::backup_file(std::string path) {
  if (strncmp(path.c_str(), prefix.c_str(), prefix.size()) != 0) {
    fprintf(stderr,
            "[FILTER] attempting to backup file: %s without correct prefix\n",
            path.c_str());
    return;
  }
  std::string back_file(path);
  back_file.append(suffix);
  printf("[FILTER] Copying %s to %s\n", path.c_str(), back_file.c_str());
  std::ifstream src(path, std::ios::binary);
  std::ofstream dst(back_file, std::ios::binary);
  dst << src.rdbuf();
}

void Manager::restore_files() {
  // recursively iterate over any things
  std::queue<std::string> paths;
  paths.push(prefix);
  struct stat s;
  while (!paths.empty()) {
    std::string path = paths.front();
    paths.pop();
    if (stat(path.c_str(), &s) == 0) {
      if (s.st_mode & S_IFDIR) {
        if (path[0] != '/') {
          continue;
        }
        // directory
        DIR *dir;
        struct dirent *ent;
        if ((dir = opendir(path.c_str())) != NULL) {
          while ((ent = readdir(dir)) != NULL) {
            std::string entry(ent->d_name);
            if (entry.compare(".") == 0 || entry.compare("..") == 0) {
              continue;
            }
            std::string full_path(path);
            full_path.append("/");
            full_path.append(entry);
            paths.push(full_path);
          }
          closedir(dir);
        } else {
          fprintf(stderr, "[FILTER] failed to open directory of %s: %s\n",
                  path.c_str(), strerror(errno));
          exit(1);
        }
      } else if (s.st_mode & S_IFREG) {
        // regular file
        if (path.length() > suffix.length() &&
            path.compare(path.length() - suffix.length(), suffix.length(),
                         suffix) != 0) {
          std::string back_file(path);
          back_file.append(suffix);
          printf("[FILTER] Copying %s to %s\n", back_file.c_str(),
                 path.c_str());
          std::ifstream src(back_file, std::ios::binary);
          std::ofstream dst(path, std::ios::binary);
          dst << src.rdbuf();
        }
      } else {
        fprintf(stderr, "[FILTER] unexpected filetype of: %s\n", path.c_str());
        exit(1);
      }
    } else {
      if (errno != ENOENT) {
        perror("[FILTER] unexpected stat failure: ");
        exit(1);
      }
    }
  }
}

void Manager::handle_open(bool at) {
  char orig_file[PATH_MAX];

  int arg = at ? 2 : 1;
  _read_file(child, orig_file, arg);
  printf("[FILTER] handling open%s: %s\n", at ? "at" : "", orig_file);

  if (strncmp(prefix.c_str(), orig_file, strlen(prefix.c_str())) == 0) {
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

void Manager::handle_fsync() {
  printf("[FILTER] handling fsync\n");

  int fd = (int)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RDI, 0);

  auto it = fds.find(fd);
  if (it != fds.end()) {
    auto orig_file = it->second;
    if (orig_file[0] != '/') {
      fprintf(stderr, "relative path rip\n");
      exit(1);
    }

    backup_file(orig_file);
  }
}

void Manager::handle_socket() {
  printf("[FILTER] handling socket\n");

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
      printf("[FILTER] sockfd: %lu\n", fd);
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
    printf("[FILTER] writing %s:%d to proc for %d\n",
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

void Manager::handle_accept() {
  printf("[FILTER] handling accept\n");
  int status;
  ptrace(PTRACE_SYSCALL, child, 0, 0);
  waitpid(child, &status, 0);
  if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, child, 0, &regs);

    if ((int)regs.rax >= 0) {
      sockaddr_in from_addr;
      _read_from_proc(child, (char *)&from_addr, (char *)regs.rsi,
                      sizeof(sockaddr_in));

      printf("[FILTER] accept from %s:%d on %d\n",
             inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port),
             (int)regs.rax);
      fdmap.node_accept_fd(my_idx, (int)regs.rax, from_addr);
    }
  }
}

const long long i1e3 = 1000;
const long long i1e6 = 1000 * 1000;
const long long i1e9 = 1000 * 1000 * 1000;

void Manager::handle_gettimeofday() {
  printf("[FILTER] handling gettimeofday\n");

  int status;
  ptrace(PTRACE_SYSCALL, child, 0, 0);
  waitpid(child, &status, 0);
  if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, child, 0, &regs);
    if (regs.rsi != 0) {
      fprintf(stderr, "[FILTER] Non-NULL timezone arg: %p\n", (void *)regs.rsi);
      exit(1);
    } else if (regs.rax != 0) {
      fprintf(stderr, "[FILTER] gettimeofday failed\n");
      exit(1);
    }

    struct timeval new_tv;
    new_tv.tv_sec = vtime.tv_sec;
    new_tv.tv_usec = vtime.tv_nsec / i1e3;
    printf("[FILTER] writing {sec: %ld, usec: %ld} as time\n", new_tv.tv_sec,
           new_tv.tv_usec);
    _write_to_proc(child, (char *)&new_tv, (char *)regs.rdi,
                   sizeof(struct timeval));
  }
}

void Manager::handle_clock_gettime() {
  printf("[FILTER] handling clock_gettime()\n");

  int status;
  ptrace(PTRACE_SYSCALL, child, 0, 0);
  waitpid(child, &status, 0);
  if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, child, 0, &regs);
    if (regs.rax != 0) {
      fprintf(stderr, "[FILTER] clock_gettime failed\n");
      exit(1);
    }

    printf("[FILTER] writing {sec: %ld, nsec: %ld} as time\n", vtime.tv_sec,
           vtime.tv_nsec);
    _write_to_proc(child, (char *)&vtime, (char *)regs.rsi,
                   sizeof(struct timespec));
  }
}

void Manager::handle_poll() {
  // get timeout param
  struct user_regs_struct regs;
  ptrace(PTRACE_GETREGS, child, 0, &regs);
  struct timespec old_timeout;
  switch (regs.orig_rax) {
  case SYS_poll: {
    printf("[FILTER] handling poll\n");
    int timeout_ms = regs.rdx;
    regs.rdx = 0;
    printf("[FILTER] overwrite poll timeout from %dms to 0\n", timeout_ms);
    ptrace(PTRACE_SETREGS, child, 0, &regs);

    old_timeout.tv_sec = timeout_ms / i1e3;
    old_timeout.tv_nsec = (timeout_ms % i1e3) * i1e6;
    break;
  }
  case SYS_select: {
    printf("[FILTER] handling select\n");
    struct timeval old_us;

    long timeout_addr = regs.r8;
    _read_from_proc(child, (char *)&old_us, (char *)timeout_addr,
                    sizeof(struct timeval));
    old_timeout.tv_sec = old_us.tv_sec;
    old_timeout.tv_nsec = old_us.tv_usec * i1e3;

    printf("[FILTER] overwrite poll timeout from {sec: %ld, usec: %ld} to 0\n",
           old_us.tv_sec, old_us.tv_usec);
    old_us.tv_sec = 0;
    old_us.tv_usec = 0;
    long addr_to_write = regs.rsp - sizeof(struct timeval) - 64;
    _write_to_proc(child, (char *)&old_us, (char *)addr_to_write,
                   sizeof(struct timeval));
    regs.r8 = addr_to_write;
    ptrace(PTRACE_SETREGS, child, 0, &regs);
    break;
  }
  default:
    fprintf(stderr, "[FILTER] unhandled polling syscall: %llu\n",
            regs.orig_rax);
    exit(1);
  }

  int status;
  ptrace(PTRACE_SYSCALL, child, 0, 0);
  waitpid(child, &status, 0);
  if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
    ptrace(PTRACE_GETREGS, child, 0, &regs);
    int retval = regs.rax;
    if (retval == 0) {
      // no updates to polling, which means the program waited the entire time.
      printf("[FILTER] no results. updating vtime\n");
      increment_vtime(old_timeout.tv_sec, old_timeout.tv_nsec);
    } else {
      // there were updates. just don't increment offset?
      printf("[FILTER] found results. no additional updates to vtime\n");
    }
  }
}

void Manager::increment_vtime(long sec, long nsec) {
  vtime.tv_nsec += nsec;
  vtime.tv_sec += sec + (vtime.tv_nsec / i1e9);
  vtime.tv_nsec %= i1e9;
}

} // namespace Filter