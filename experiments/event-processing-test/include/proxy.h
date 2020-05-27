#pragma once

#include <netinet/ip.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syscall.h>

#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "fdmap.h"

// This ensures requests going to the proxy's address go to the node's address.
// The proxy will be the one establishing connections to the node on behalf of
// any request.
// It listens on <my_addr> and forwards any messages to <node_addr>.
// It reports what messages are in-flight
class Proxy {
public:
  Proxy(FdMap &fdmap, std::vector<sockaddr_in> actual_node_map,
        std::vector<sockaddr_in> proxy_node_map, size_t num_clients);

  void set_alive(int idx);

  // starts or stops the given node
  // returns list of (node, nodefd) to notify of death if stopping
  std::vector<std::pair<int, int>> toggle_node(int idx);

  // check for messages, blocking for a new message if needed
  bool poll_for_events(bool blocking);

  // retrieves all inbound fds for a given node that have waiting messages
  std::vector<int> get_fds_with_msgs(int idx);

  // checks if fd has more to send
  bool has_more(int fd);

  // send the next message waiting to be sent on the given fd. notifies
  // orchestrator if it should wait extra for closed connection
  bool allow_next_msg(int fd);

  void print_state();

private:
  // track if each node is alive
  std::unordered_map<int, bool> node_alive;

  // epoll fd
  int efd;
  // listening sockets for each of the proxies
  std::vector<int> sockfds;
  // actual address for each of the nodes
  std::vector<sockaddr_in> actual_node_map;
  // address for each of the proxies
  std::vector<sockaddr_in> proxy_node_map;

  FdMap &fdmap;

  // all inbound fds for a given destination node
  std::unordered_map<int, std::set<int>> inbound_fds;
  // fd -> destination node idx
  std::unordered_map<int, int> fd_to_node;
  // fd -> proxied fd (2-way)
  std::unordered_map<int, int> related_fd;

  // fd -> queue of messages
  std::unordered_map<int, std::deque<std::vector<char>>> waiting_msgs;

  int create_listen(int idx);
  std::vector<std::pair<int, int>> stop_node(int idx);

  void register_fd(int fd);
  void unregister_fd(int fd,
                     std::vector<std::pair<int, int>> *related_nodes = nullptr);
  void link_fds(int fd1, int fd2);
};