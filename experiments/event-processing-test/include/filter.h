#pragma once

#include <netinet/ip.h>
#include <stdint.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <syscall.h>
#include <time.h>

#include <deque>
#include <functional>
#include <list>
#include <map>
#include <unordered_map>

#include "fdmap.h"

namespace Filter {

const uint32_t syscalls_intercept[] = {
    // polling-related
    SYS_select,
    SYS_poll,
    SYS_gettimeofday,
    SYS_clock_gettime,
    SYS_getrandom,
    // filesystem-related
    SYS_open,
    SYS_openat,
    SYS_mknod,
    SYS_mknodat,
    SYS_creat,
    SYS_close,
    SYS_write, // assume that network doesn't use write
    SYS_rename,
    SYS_renameat, // not handled due to scope
    SYS_syncfs,
    SYS_fsync,
    SYS_fdatasync,
    // network-related
    SYS_socket,
    SYS_bind,
    SYS_getsockname,
    SYS_getpeername,
    SYS_accept,
    SYS_accept4,
    SYS_connect,
    SYS_sendto,
    SYS_recvfrom,
};

enum Event {
  EV_POLLING,
  EV_EXIT,
  EV_DEAD,
  EV_RANDOM,
  EV_CONNECT,
  EV_SENDTO,
  EV_FSYNC,
  EV_WRITE,
};

enum State {
  ST_DEAD,
  ST_POLLING,
  ST_STOPPED,
  ST_RANDOM,
  ST_FILES,
  ST_NETWORK,
};

class Manager {
public:
  Manager(int node_idx, std::vector<std::string> command, sockaddr_in old_addr,
          sockaddr_in new_addr, FdMap &fdmap, std::string prefix,
          bool ignore_stdout);

  Event to_next_event();

  // allow an event to occur. returns metadata about the event's status
  // has cmd for some arbitrary command for some event
  int allow_event(Event ev);

  void setup_validate();
  void finish_validate();

  void handle_fsync(Event ev, std::function<size_t(size_t)> num_ops_fn);
  int handle_write(Event ev, std::function<size_t(size_t)> to_write_fn);
  void handle_getrandom(Event ev, std::function<void(void *, size_t)> fill_fn);

  void toggle_node();

private:
  int my_idx;

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

  // mapping between proxy fds and node fds
  FdMap &fdmap;
  // how to redirect addresses
  sockaddr_in old_addr, new_addr;
  // socket file descriptors
  // value: is_redirected
  std::unordered_map<int, bool> sockfds;

  // directory in the file system that should be tracked with fsync
  // TODO (this isn't necessarily needed, but does make our life easier)
  std::string prefix;
  // suffix to append to "properly fsynced" files
  static const std::string suffix;
  // filesystem-related file descriptors
  // value: current path
  std::unordered_map<int, std::string> fds;
  // latest version of a file
  std::unordered_map<std::string, int> file_vers;
  // latest persisted version of a file
  std::unordered_map<std::string, int> file_pers;
  // all pending op indexes for a given file
  // file -> [pending op]
  std::unordered_map<std::string, std::deque<size_t>> file_pending;
  // set of files whose latest version is a src of a rename
  std::unordered_set<std::string> rename_srcs;
  size_t ops_done;
  size_t op_count;
  // rename ops (for happens-before relations)
  // op idx -> ((src_file, src_version), (dst_file, dst_version))
  std::map<size_t,
           std::pair<std::pair<std::string, int>, std::pair<std::string, int>>>
      pending_ops;

  std::vector<std::pair<std::string, std::string>> restore_map;

  void start_node();
  void stop_node();

  void backup_file(int fd);
  void restore_files();

  // should set timeout to 0
  void handle_poll();
  void handle_select();

  void handle_gettimeofday();
  void handle_clock_gettime();

  void handle_open(bool at);
  void handle_mknod(int arg);
  int handle_rename();
  void perform_next_op();
  std::string get_backup_filename(std::string file, int version);
  std::pair<std::string, int> find_root(std::string file, int version);

  void handle_socket(); // only track AF_INET, SOCK_STREAM, IPPROTO_IP addresses
                        // (hard to say if this is actually needed)
  void handle_bind();   // redirect bind to some other addr
  void handle_close();
  void handle_getsockname(); // redirect back to original addr
  void handle_accept();
  int handle_connect();
  int handle_sendto();

  void increment_vtime(long sec, long nsec);
};

} // namespace Filter