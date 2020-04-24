#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
} // namespace

namespace Filter {

Manager::Manager(pid_t pid) : child{pid}, fds() {
  int status;
  waitpid(pid, &status, 0);
  ptrace(PTRACE_SETOPTIONS, pid, 0,
         PTRACE_O_TRACESECCOMP | PTRACE_O_TRACESYSGOOD);
}

bool Manager::to_next_event() {
  int status;

  while (1) {
    ptrace(PTRACE_CONT, child, 0, 0);
    waitpid(child, &status, 0);
    // printf("[waitpid status: 0x%08x]\n", status);
    if (status >> 8 == (SIGTRAP | (PTRACE_EVENT_SECCOMP << 8))) {
      // check if it's one of the syscalls we want to handle
      int my_syscall =
          ptrace(PTRACE_PEEKUSER, child, sizeof(long) * ORIG_RAX, 0);
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
      default:
        printf("ignored syscall: %d\n", my_syscall);
        continue;
      }
    }
    if (WIFEXITED(status)) {
      printf("exited\n");
      return false;
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

} // namespace Filter