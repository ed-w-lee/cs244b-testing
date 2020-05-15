#pragma once

#include <netinet/ip.h>
#include <stdint.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <syscall.h>
#include <time.h>
#include <unordered_map>

namespace Filter {

const uint32_t syscalls_intercept[] = {
    // polling-related
    SYS_select,
    SYS_poll,
    SYS_gettimeofday,
    SYS_clock_gettime,
    // filesystem-related
    SYS_open,
    SYS_openat,
    SYS_syncfs,
    SYS_fsync,
    SYS_fdatasync,
    // SYS_stat,
    // SYS_mknod,
    // network-related
    SYS_socket,
    SYS_bind,
    SYS_getsockname,
    SYS_getpeername,
    SYS_sendto,
    SYS_recvfrom,
};

enum Event {
  EV_POLLING,
  EV_EXIT,
  EV_DEAD,
  EV_NETWORK,
  EV_SYNCFS,
};

enum State {
  ST_DEAD,
  ST_POLLING,
  ST_STOPPED,
  ST_WAITING_FSYNC,
  ST_NETWORK,
};

class Manager {
public:
  Manager(std::vector<std::string> command, sockaddr_in old_addr,
          sockaddr_in new_addr, std::string prefix, bool ignore_stdout);

  Event to_next_event();

  // allow an event to occur. returns if the event succeeded or not.
  int allow_event(Event ev);

  void sync_fs();

  void sync_file(int fd);

  void receive_msg(int sockfd);

  void toggle_node();

private:
  // the command being run by the manager for starting the node
  std::vector<std::string> command;

  // current time for the process. mainly to prevent orchestrator runtime
  // variance from changing child behavior
  struct timespec vtime;

  // pid of the node (if it's running)
  pid_t child;

  // whether we should redirect stdout to /dev/null
  bool ignore_stdout;

  // state of the child (running, stopped, etc.)
  State child_state;

  // prefix of files in the file system that should be tracked with fsync
  // (this isn't necessarily needed, but does make our life easier)
  std::string prefix;

  // how to redirect addresses
  sockaddr_in old_addr, new_addr;

  // tracking filesystem and socket file descriptors
  std::unordered_map<int, std::string> fds; // value: absolute path of file
  std::unordered_map<int, bool> sockfds;    // value: is_redirected

  // suffix to append to "properly fsynced" files
  static const std::string suffix;

  void start_node();
  void stop_node();

  void backup_file(std::string path);
  void restore_files();

  // should set timeout to 0
  void handle_poll();
  void handle_select();

  void handle_gettimeofday();
  void handle_clock_gettime();

  void handle_open(bool at);
  void handle_fsync();

  void handle_socket(); // only track AF_INET, SOCK_STREAM, IPPROTO_IP addresses
                        // (hard to say if this is actually needed)
  void handle_bind();   // redirect bind to some other addr
  void handle_getsockname(); // redirect back to original addr
  void handle_connect();

  void increment_vtime(long sec, long nsec);
};

} // namespace Filter