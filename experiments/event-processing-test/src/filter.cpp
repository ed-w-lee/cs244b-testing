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

#include <algorithm>
#include <functional>
#include <queue>
#include <random>
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

bool _startswith(std::string str, std::string prefix) {
  return str.length() >= prefix.length() &&
         strncmp(str.c_str(), prefix.c_str(), prefix.length()) == 0;
}

bool _endswith(std::string str, std::string suffix) {
  return (str.length() >= suffix.length() &&
          str.compare(str.length() - suffix.length(), suffix.length(),
                      suffix) != 0);
}

bool _exists(std::string file) {
  struct stat buf;
  if (stat(file.c_str(), &buf) < 0) {
    if (errno == ENOENT) {
      return false;
    } else {
      fprintf(stderr, "[FILTER] failed to stat %s due to %s\n", file.c_str(),
              strerror(errno));
      exit(1);
    }
  }
  return true;
}

void _remove_dir(const char *path) {
  struct dirent *entry = NULL;
  DIR *dir = NULL;
  if ((dir = opendir(path))) {
    while ((entry = readdir(dir))) {
      DIR *sub_dir = NULL;
      FILE *file = NULL;
      char abs_path[PATH_MAX] = {0};
      if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
        sprintf(abs_path, "%s/%s", path, entry->d_name);
        if ((sub_dir = opendir(abs_path))) {
          closedir(sub_dir);
          _remove_dir(abs_path);
        } else {
          if ((file = fopen(abs_path, "r"))) {
            fclose(file);
            remove(abs_path);
          }
        }
      }
    }
  }
  remove(path);
}

} // namespace

namespace Filter {

const std::string Manager::suffix = ".__bk";

Manager::Manager(int my_idx, std::vector<std::string> command,
                 sockaddr_in old_addr, sockaddr_in new_addr, FdMap &fdmap,
                 std::string prefix, bool ignore_stdout)
    : my_idx(my_idx), command(command), ignore_stdout(ignore_stdout),
      fdmap(fdmap), old_addr(old_addr), new_addr(new_addr), prefix(prefix) {
  printf("[FILTER] creating with command: ");
  for (auto str : command) {
    printf("%s ", str.c_str());
  }
  printf("\n");

  // // recursively clear anything in prefix
  // _remove_dir(prefix.c_str());

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
      strcat(blah, inet_ntoa(old_addr.sin_addr));
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
    printf("Executing %d with ", getpid());
    for (size_t i = 0; i < command.size(); i++) {
      printf("<%s> ", args[i]);
    }
    printf("\n");
    exit(execv(args[0], (char **)args));
  }

  child = pid;

  int status = 0;
  waitpid(pid, &status, 0);
  ptrace(PTRACE_SETOPTIONS, pid, 0,
         PTRACE_O_TRACESECCOMP | PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEEXEC);

  {
    // disable vDSO to ensure we can intercept gettimeofday
    // reference: https://stackoverflow.com/a/52402306

    // get to exec point
    while (status >> 8 != (SIGTRAP | (PTRACE_EVENT_EXEC << 8))) {
      ptrace(PTRACE_CONT, child, NULL, NULL);
      waitpid(pid, &status, 0);
    }
    // before exec starts, we overwrite AT_SYSINFO_EHDR
    char todo[2000];
    long rsp = ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RSP, 0);
    size_t num_to_get = command.size() + 70;
    _read_from_proc(child, (char *)todo, (char *)rsp,
                    num_to_get * sizeof(long));
    int num_nulls = 0;
    int auxv_idx = 0;
    bool did_overwrite = false;
    for (size_t i = 0; i < num_to_get; i++) {
      long ptr = *(long *)(&todo[i * sizeof(long)]);
      if (num_nulls == 2) {
        auxv_idx++;
        if (auxv_idx == 2) {
          // AT_SYSINFO_EHDR (vDSO location) experimentally found to be 2nd
          // element in aux vector (at least in my OS **thinking**)
          printf("[FILTER] overwriting vDSO: %lx\n", ptr);
          did_overwrite = true;
          long null = 0;
          _write_to_proc(child, (char *)&null, (char *)rsp + (i * sizeof(long)),
                         sizeof(long));
        }
      }
      if (ptr == 0) {
        num_nulls++;
      }
    }
    if (!did_overwrite) {
      fprintf(stderr, "[FILTER] Failed to overwrite vDSO\n");
      exit(1);
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

  fds.clear();
  sockfds.clear();
  // delete any unnecessary files for GC
  for (auto &op : pending_ops) {
    auto src = op.second.first;
    std::string try_delete = get_backup_filename(src.first, src.second);
    if (_exists(try_delete)) {
      unlink(try_delete.c_str());
    }
  }
  for (auto &tup : file_vers) {
    std::string latest_write(tup.first);
    latest_write.append(".latest").append(suffix);
    std::ofstream out(latest_write);
    std::ifstream in(tup.first);
    out << in.rdbuf();
  }
  // clear any operation-related structures
  file_pending.clear();
  ops_done = 0;
  op_count = 0;
  pending_ops.clear();
  rename_srcs.clear();

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
        case SYS_getrandom:
          child_state = ST_RANDOM;
          return EV_RANDOM;

          // filesystem-related
        case SYS_openat:
          handle_open(true);
          continue;
        case SYS_creat:
          // handle like open since pathname in same place and we're not
          // thinking too deeply about mode right now
          // TODO think a bit more deeply about mode?
        case SYS_open:
          handle_open(false);
          continue;
        case SYS_mknod:
          handle_mknod(1);
          continue;
        case SYS_mknodat:
          handle_mknod(2);
          continue;

        case SYS_write:
          child_state = ST_FILES;
          return EV_WRITE;
        case SYS_rename:
          handle_rename();
          continue;
        case SYS_renameat:
          // TODO if time, handle renameat
          fprintf(stderr, "unaccounted for renameat\n");
          exit(1);
          break;
        case SYS_syncfs:
          // TODO if time, handle syncfs
          fprintf(stderr, "unaccounted for syncfs\n");
          exit(1);
          break;
        case SYS_fdatasync:
        case SYS_fsync:
          child_state = ST_FILES;
          return EV_FSYNC;

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
          child_state = ST_NETWORK;
          return EV_CONNECT;
        case SYS_sendto:
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
    // TODO adjust for feedback from orch
  case EV_CONNECT: {
    return handle_connect();
  }
  case EV_SENDTO: {
    return handle_sendto();
  }
  default:
    fprintf(stderr, "[FILTER] invalid event to allow\n");
    exit(1);
  }
  return -1;
}

void Manager::backup_file(int fd) {
  std::string curr_loc = fds[fd];
  printf("[FILTER] starting backup_file on %s\n", curr_loc.c_str());

  struct stat s;
  if (stat(curr_loc.c_str(), &s) == 0) {
    if (S_ISDIR(s.st_mode)) {
      printf("[FILTER] backing up directory\n");
      // perform rename ops until no more pending ops for any files in the dir
      size_t last_rename = 0;
      for (auto it = file_pending.begin(); it != file_pending.end(); it++) {
        if (_startswith(it->first, curr_loc) &&
            it->first.find('/', curr_loc.length() + 1) == it->first.npos) {
          // direct child of dir, should perform all ops out of or into
          if (it->second.back() > last_rename) {
            last_rename = it->second.back();
          }
        }
      }
      while (ops_done < last_rename) {
        perform_next_op();
      }
    } else if (S_ISREG(s.st_mode)) {
      // persist data based on dependencies
      int curr_vers = file_vers[curr_loc];
      file_pers[curr_loc] = curr_vers;
      std::string my_file = get_backup_filename(curr_loc, curr_vers);
      bool has_root = false;
      if (!_exists(my_file)) {
        auto root = find_root(curr_loc, curr_vers);
        if (curr_loc.compare(root.first) != 0 || curr_vers != root.second) {
          has_root = true;
          // we have a root, which means we should persist data at the root
          // rather than at our filename (since the renames haven't happened
          // yet)
          std::string out_file = get_backup_filename(root.first, root.second);
          printf("[FILTER] Found root for fsync. Copying %s to %s\n",
                 curr_loc.c_str(), out_file.c_str());
          std::ifstream src(curr_loc, std::ios::binary);
          std::ofstream dst(out_file, std::ios::binary);
          dst << src.rdbuf();
        }
      }
      if (!has_root) {
        // we don't have a root, so we should persist the file as the current
        // version (we don't have to create a new version since we are now
        // guaranteed that rename ops should be based off this version)
        // TODO it's probably more correct to persist after all file changes,
        // but it _should_ be fine due to versioning
        std::string out_file = get_backup_filename(curr_loc, curr_vers);
        printf("[FILTER] No root for fsync. Copying %s to %s\n",
               curr_loc.c_str(), out_file.c_str());
        std::ifstream src(curr_loc, std::ios::binary);
        std::ofstream dst(out_file, std::ios::binary);
        dst << src.rdbuf();
      }
    } else {
      fprintf(stderr, "[FILTER] unexpected filetype of: %s\n",
              curr_loc.c_str());
      exit(1);
    }
  } else {
    if (errno != ENOENT) {
      fprintf(stderr, "[FILTER] unexpected stat failure %s due to %s\n",
              curr_loc.c_str(), strerror(errno));
      exit(1);
    }
  }
}

std::pair<std::string, int> Manager::find_root(std::string file, int version) {
  printf("[FILTER] starting find_root\n");
  auto it = pending_ops.rbegin();
  for (; it != pending_ops.rend(); it++) {
    auto op = it->second;
    auto src = op.first;
    auto dst = op.second;
    if (dst.first.compare(file) == 0 && version == dst.second) {
      file = src.first;
      version = src.second;
    }
  }
  return {file, version};
}

void Manager::restore_files() {
  printf("[FILTER] starting restore_files\n");
  for (auto &tup : file_vers) {
    printf("[FILTER] Attempting to restore %s\n", tup.first.c_str());
    std::string back_file =
        get_backup_filename(tup.first, file_pers.at(tup.first));
    if (_exists(back_file)) {
      printf("[FILTER] Copying %s to %s\n", back_file.c_str(),
             tup.first.c_str());
      std::ifstream src(back_file, std::ios::binary);
      std::ofstream dst(tup.first, std::ios::binary);
      dst << src.rdbuf();
    } else {
      printf("[FILTER] %s not found, nop\n", back_file.c_str());
    }
  }
}

void Manager::handle_open(bool at) {
  char orig_file[PATH_MAX];

  int arg = at ? 2 : 1;
  _read_file(child, orig_file, arg);
  printf("[FILTER] handling open%s: %s\n", at ? "at" : "", orig_file);
  std::string to_open(orig_file);

  if (_startswith(to_open, prefix)) {
    // store existence of file to check if it was created
    bool exists =
        file_vers.find(to_open) != file_vers.end()
            ? _exists(get_backup_filename(to_open, file_vers[to_open]))
            : false;
    // get file descriptor for the opened file so we can register it
    int status;
    ptrace(PTRACE_SYSCALL, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
      int fd = (int)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RAX, 0);
      printf("return val: %d\n", fd);
      if (fd >= 0) {
        if (to_open[to_open.size() - 1] == '/') {
          to_open = to_open.substr(0, to_open.size() - 1);
        }
        fds[fd] = to_open;
        // only maintain versions for regular files (not directories)
        struct stat s;
        if (stat(to_open.c_str(), &s) == 0 && S_ISREG(s.st_mode)) {
          printf("[FILTER] opened a regular file, track file_vers\n");
          if (file_vers.find(to_open) == file_vers.end()) {
            file_vers[to_open] = 0;
            file_pers[to_open] = 0;
          }
          if (!exists) {
            file_vers[to_open]++;
          }
        }
      }
    }
  }
}

void Manager::handle_mknod(int arg) {
  char orig_file[PATH_MAX];

  _read_file(child, orig_file, arg);
  printf("[FILTER] handling mknod: %s\n", orig_file);
  std::string to_mknod(orig_file);

  if (_startswith(to_mknod, prefix)) {
    // get file descriptor for the opened file so we can register it
    int status;
    ptrace(PTRACE_SYSCALL, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
      uint64_t ret =
          (uint64_t)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RAX, 0);
      printf("return val: %lu\n", ret);
      if (ret == 0) {
        if (to_mknod[to_mknod.size() - 1] == '/') {
          to_mknod = to_mknod.substr(0, to_mknod.size() - 1);
        }
        file_vers[to_mknod]++;
      }
    }
  }
}

int Manager::handle_rename() {
  printf("[FILTER] handling rename\n");

  char src[PATH_MAX];
  char dst[PATH_MAX];
  _read_file(child, src, 1);
  _read_file(child, dst, 2);
  printf("[FILTER] renaming %s to %s\n", src, dst);
  if (strncmp(prefix.c_str(), src, strlen(prefix.c_str())) == 0 &&
      strncmp(prefix.c_str(), dst, strlen(prefix.c_str())) == 0) {
    int status;
    ptrace(PTRACE_SYSCALL, child, 0, 0);
    waitpid(child, &status, 0);
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
      uint64_t ret =
          (uint64_t)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RAX, 0);
      printf("[FILTER] return val: %lu\n", ret);
      if (ret == 0) {
        struct stat s;
        if (stat(dst, &s) != 0) {
          fprintf(stderr, "[FILTER] stat failed on %s with %s\n", src,
                  strerror(errno));
          exit(1);
        } else if (!S_ISREG(s.st_mode)) {
          // TODO Support directories for rename (ugh)
          fprintf(stderr, "[FILTER] rename on directories not supported\n");
          exit(1);
        }
        std::string src_str(src);
        std::string dst_str(dst);
        // register new rename op
        // TODO can we somehow GC pending ops that don't matter anymore?
        op_count++;
        file_vers[dst_str]++;
        pending_ops[op_count] = {{src_str, file_vers[src_str]},
                                 {dst_str, file_vers[dst_str]}};
        if (file_pending.find(dst_str) == file_pending.end()) {
          file_pending[dst_str] = {};
        }
        if (file_pending.find(src_str) == file_pending.end()) {
          file_pending[src_str] = {};
        }
        file_pending[dst_str].push_back(op_count);
        file_pending[src_str].push_back(op_count);
        printf("[FILTER] registered new rename op %lu for (%s,%d) -> (%s,%d)\n",
               op_count, src, file_vers[src_str], dst, file_vers[dst_str]);

        // update current locations of any open fds
        for (auto &tup : fds) {
          if (tup.second.compare(src_str)) {
            tup.second = dst_str;
          }
        }
        file_vers[src_str]++;
      }
      return ret;
    }
    // should be unreachable
    fprintf(stderr, "[FILTER] syscall continuation didn't work\n");
    exit(1);
    return -1;
  } else {
    fprintf(stderr, "[FILTER] rename on non-prefixed files, exiting\n");
    exit(1);
    return -1; // should be unreachable
  }
}

void Manager::perform_next_op() {
  auto it = pending_ops.begin();

  auto op = it->second;
  auto src = op.first;
  std::string src_file = get_backup_filename(src.first, src.second);
  auto dst = op.second;
  std::string dst_file = get_backup_filename(dst.first, dst.second);
  printf("[FILTER] performing op %lu (%s -> %s)\n", it->first, src_file.c_str(),
         dst_file.c_str());
  if (!_exists(src_file)) {
    std::ofstream to_create(dst_file);
  } else {
    // file exists, move to new file
    if (rename(src_file.c_str(), dst_file.c_str()) < 0) {
      fprintf(stderr, "[FILTER] perform_next_op failed to rename due to %s\n",
              strerror(errno));
      exit(1);
    }
  }
  if (file_pending[dst.first].front() != it->first) {
    fprintf(stderr, "[FILTER] mismatching pending idx for dst: %lu vs. %lu\n",
            file_pending[dst.first].front(), it->first);
    exit(1);
  }
  if (file_pending[src.first].front() != it->first) {
    fprintf(stderr, "[FILTER] mismatching pending idx for src: %lu vs. %lu\n",
            file_pending[src.first].front(), it->first);
    exit(1);
  }
  file_pending[dst.first].pop_front();
  file_pending[src.first].pop_front();
  file_pers[src.first] = std::max(file_pers[src.first], src.second + 1);
  file_pers[dst.first] = std::max(file_pers[dst.first], dst.second);
  printf("[FILTER] updating file_pers to src: %d and dst: %d\n",
         file_pers[src.first], file_pers[dst.first]);
  ops_done = it->first;
  pending_ops.erase(it);
}

void Manager::handle_fsync(Event ev, std::function<size_t(size_t)> num_ops_fn) {
  if (ev != EV_FSYNC) {
    fprintf(stderr, "wrong event for handle_fsync\n");
    exit(1);
  }
  printf("[FILTER] handling fsync\n");
  child_state = ST_STOPPED;

  // do some user-defined number of pending operations
  size_t ops_to_do = num_ops_fn(pending_ops.size());
  printf("[FILTER] performing %lu ops before fsync\n", ops_to_do);
  for (size_t i = 0; i < ops_to_do; i++) {
    perform_next_op();
  }

  int fd = (int)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RDI, 0);
  auto it = fds.find(fd);
  if (it != fds.end()) {
    backup_file(fd);
  } else {
    fprintf(stderr, "[FILTER] fsync called on fd not accounted fd %d\n", fd);
    exit(1);
  }
}

int Manager::handle_write(Event ev, std::function<size_t(size_t)> to_write_fn) {
  if (ev != EV_WRITE) {
    fprintf(stderr, "wrong event for handle_write\n");
  }
  printf("[FILTER] handling write\n");
  child_state = ST_STOPPED;

  struct user_regs_struct regs;
  ptrace(PTRACE_GETREGS, child, 0, &regs);
  int fd = regs.rdi;
  size_t count = regs.rdx;

  if (fds.find(fd) != fds.end()) {
    // if this is a filesystem fd into prefix, allow write corruption on it
    size_t to_write = to_write_fn(count);
    if (to_write == count) {
      // normal write, just let it go through
    } else {
      // we are failing the entire node after doing a partial write
      printf("[FILTER] write %lu chars before syncing and failing\n", to_write);
      regs.rdx = to_write;
      ptrace(PTRACE_SETREGS, child, 0, &regs);
      int status;
      ptrace(PTRACE_SYSCALL, child, 0, 0);
      waitpid(child, &status, 0);
      if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
        ssize_t ret =
            (ssize_t)ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RAX, 0);
        printf("[FILTER] ret: %lu\n", ret);

        // flush all dentry changes and sync fd
        while (!pending_ops.empty()) {
          perform_next_op();
        }
        backup_file(fd);

        return -1;
      }
    }
  }
  return count;
}

std::string Manager::get_backup_filename(std::string file, int version) {
  file.append(".");
  file.append(std::to_string(version));
  file.append(suffix);
  return file;
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

int Manager::handle_connect() {
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

int Manager::handle_sendto() {
  int status;
  ptrace(PTRACE_SYSCALL, child, 0, 0);
  waitpid(child, &status, 0);
  if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, child, 0, &regs);
    int sendfd = (int)regs.rdi;
    if (fdmap.is_nodefd_alive(my_idx, sendfd)) {
      printf("[FILTER] connection for %d is still alive. allow sendto\n",
             sendfd);
      return (int)regs.rax;
    } else {
      // proxy already closed. we should close this fd
      printf("[FILTER] connection for %d is dead. inject failure\n", sendfd);
      regs.rax = -((long)ECONNRESET);
      ptrace(PTRACE_SETREGS, child, 0, &regs);
      return -1;
    }
  }
  fprintf(stderr, "[FILTER] did not get subsequent syscall\n");
  exit(1);
  return -1;
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
  printf("[FILTER] handling clock_gettime\n");

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

void Manager::handle_getrandom(std::mt19937 &rng) {
  printf("[FILTER] handling getrandom\n");
  child_state = ST_STOPPED;

  int status;
  ptrace(PTRACE_SYSCALL, child, 0, 0);
  waitpid(child, &status, 0);
  if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, child, 0, &regs);
    ssize_t ret = regs.rax;
    printf("[FILTER] returned: %lu\n", ret);
    if (ret > 0) {
      // generate <ret> pseudo-random bytes
      char buf[ret + 4];
      for (int i = 0; i < ret; i += 4) {
        *(int *)(buf + i) = rng();
      }
      _write_to_proc(child, buf, (char *)regs.rdi, ret);
    }
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
      // no updates to polling, which means the program waited the entire
      // time.
      printf("[FILTER] no results. updating vtime\n");
      increment_vtime(old_timeout.tv_sec, old_timeout.tv_nsec);
    } else {
      // there were updates. just don't increment offset?
      printf("[FILTER] found %d results. no additional updates to vtime\n",
             retval);
    }
  }
}

void Manager::increment_vtime(long sec, long nsec) {
  vtime.tv_nsec += nsec;
  vtime.tv_sec += sec + (vtime.tv_nsec / i1e9);
  vtime.tv_nsec %= i1e9;
}

} // namespace Filter