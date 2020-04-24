#pragma once

#include <stdint.h>
#include <string>
#include <sys/types.h>
#include <syscall.h>
#include <unordered_map>

namespace Filter {

const uint32_t syscalls_intercept[] = {
    SYS_open, SYS_openat, SYS_fsync,   SYS_fdatasync, SYS_read,    SYS_write,
    SYS_stat, SYS_mknod,  SYS_accept4, SYS_accept,    SYS_connect, SYS_socket,
};

class Manager {
public:
  Manager(pid_t pid);

  bool to_next_event();

  void sync_file(int fd);

  void receive_msg(int sockfd);

private:
  pid_t child;
  std::unordered_map<int, std::string> fds;

  const char *suffix = ".__bk";
  const char *prefix = "/tmp/our_cs244b_test_13245646/";

  void handle_open(bool at);
  void handle_stat();
  void handle_mknod();
  void handle_fsync();
};

} // namespace Filter