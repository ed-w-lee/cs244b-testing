#pragma once

#include <netinet/ip.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syscall.h>

#include <queue>
#include <string>
#include <unordered_map>

// This ensures requests going to the proxy's address go to the node's address.
// The proxy will be the one establishing connections to the node on behalf of
// any request.
// It listens on <my_addr> and forwards any messages to <node_addr>.
// It reports what messages are in-flight
class Proxy {
public:
  Proxy(sockaddr_in my_addr, sockaddr_in node_addr,
        std::vector<sockaddr_in> node_map);

  // retrieves all messages intended for the node
  // (node idx or client) -> queue of messages
  const std::unordered_map<int, std::queue<std::string>> &get_msgs();

  // TCP semantics, so node<->node should be in order
  void allow_next_msg(int node_idx);

private:
  const int CLIENT_OFFS = 1000;

  int efd, sockfd;
  sockaddr_in node_addr;
  std::vector<sockaddr_in> node_map;
  std::unordered_map<int, int> fd_to_node;

  // (node idx or client) -> queue of messages
  std::unordered_map<int, std::queue<std::string>> waiting_msgs;

  void poll_for_events();
};