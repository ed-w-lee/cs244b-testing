#pragma once

#include <netinet/ip.h>
#include <stdint.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <syscall.h>
#include <time.h>
#include <unordered_map>

#include "fdmap.h"

namespace ClientFilter {

const uint32_t syscalls_intercept[] = {
    // network-related
    SYS_socket, SYS_close, SYS_connect, SYS_sendto, SYS_recvfrom,
};

enum Event {
  EV_DEAD,
  EV_EXIT,
  EV_CONNECT,
  EV_SENDTO,
  EV_CLOSE,
  EV_RECVING,
};

enum State {
  ST_DEAD,
  ST_STOPPED,
  ST_RECVING,
  ST_NETWORK,
};

const int CLIENT_OFFS = 1000;

class ClientManager {
public:
  ClientManager(int client_idx, std::vector<std::string> command, FdMap &fdmap,
                bool ignore_stdout);

  Event to_next_event();

  // allow an event to occur. returns metadata about the event's status
  int allow_event(Event ev);

  // perform a close. return true if we closed a sockfd
  bool handle_close();

  void toggle_client();

private:
  int my_idx;

  // the command being run by the manager for starting the client
  std::vector<std::string> command;

  // pid of the node (if it's running)
  pid_t child;

  // whether we should redirect stdout to /dev/null
  bool ignore_stdout;

  // state of the child (running, stopped, etc.)
  State child_state;

  // mapping between proxy fds and client fds
  FdMap &fdmap;

  void start_client();
  void stop_client();

  std::unordered_set<int> sockfds;

  void handle_socket();

  // if blocking, yeah just stop it
  void handle_recv();

  void increment_vtime(long sec, long nsec);
};

} // namespace ClientFilter