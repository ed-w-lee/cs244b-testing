#pragma once

#include <netinet/ip.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syscall.h>

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>

// This ensures requests going to the proxy's address go to the node's address.
// The proxy will be the one establishing connections to the node on behalf of
// any request.
// It listens on <my_addr> and forwards any messages to <node_addr>.
// It reports what messages are in-flight
class Proxy {
public:
  Proxy(sockaddr_in my_addr, sockaddr_in node_addr,
        std::vector<sockaddr_in> actual_node_map,
        std::vector<sockaddr_in> proxy_node_map);

  void toggle_node();

  // retrieves all messages intended for the node
  // (node idx or client) -> queue of messages
  const std::unordered_map<int, std::deque<std::vector<char>>> &get_msgs();

  // TCP semantics, so node<->node should be in order
  void allow_next_msg(int fd);

  void print_state();

private:
  bool node_alive;
  const int CLIENT_OFFS = 1000;

  int efd, sockfd;
  sockaddr_in node_addr;
  std::vector<sockaddr_in> actual_node_map;
  std::vector<sockaddr_in> proxy_node_map;

  std::unordered_set<int> inbound_fds;
  std::unordered_map<int, int> fd_to_node;
  std::unordered_map<int, int> related_fd;

  // waiting_key -> queue of messages
  std::unordered_map<int, std::deque<std::vector<char>>> waiting_msgs;

  void stop_node();

  void register_fd(int fd);
  void unregister_fd(int fd);
  void link_fds(int fd1, int fd2);

  bool poll_for_events();
};