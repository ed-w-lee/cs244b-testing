#pragma once

#include <netinet/ip.h>
#include <stdint.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <syscall.h>
#include <unordered_map>

namespace Filter {

const uint32_t syscalls_intercept[] = {
    // filesystem-related
    SYS_open,
    SYS_openat,
    SYS_fsync,
    SYS_fdatasync,
    SYS_stat,
    SYS_mknod,
    // network-related
    SYS_socket,
    SYS_bind,
    SYS_getsockname,
    SYS_getpeername,
    SYS_sendto,
    SYS_recvfrom,
};

class Manager {
public:
  Manager(pid_t pid, sockaddr_in old_addr, sockaddr_in new_addr);

  int to_next_event();

  void sync_fs();

  void sync_file(int fd);

  void receive_msg(int sockfd);

private:
  pid_t child;
  bool send_continue;
  sockaddr_in old_addr, new_addr;

  std::unordered_map<int, std::string> fds;
  std::unordered_map<int, bool> sockfds; // is_redirected

  const char *suffix = ".__bk";
  const char *prefix = "/tmp/our_cs244b_test_13245646/";

  void handle_open(bool at);
  void handle_stat();
  void handle_mknod();
  void handle_fsync();

  // TODO - make sure we handle non-synchronous reads / writes
  // void handle_read();
  // void handle_write();

  void handle_socket(); // only track AF_INET, SOCK_STREAM, IPPROTO_IP addresses
                        // (hard to say if this is actually needed)
  void handle_bind();   // redirect bind to some other addr
  void handle_getsockname(); // redirect back to original addr
  // void handle_accept();
  void handle_connect();
  // void handle_send();
  // void handle_recv();
};

} // namespace Filter