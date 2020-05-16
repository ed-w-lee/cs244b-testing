#pragma once

#include <netinet/ip.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syscall.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

inline size_t key(int i, int j) { return (size_t)i << 32 | (unsigned int)j; }
inline int node_from_key(size_t key) { return (int)(key >> 32); }
inline int fd_from_key(size_t key) { return (int)key; }

// This ensures requests going to the proxy's address go to the node's address.
// The proxy will be the one establishing connections to the node on behalf of
// any request.
// It listens on <my_addr> and forwards any messages to <node_addr>.
// It reports what messages are in-flight
class FdMap {
public:
  FdMap(size_t num_nodes);

  void node_connect_fd(int node, int fd);

  void proxy_accept_fd(int proxyfd);

  void proxy_connect_fd(int node, int proxyfd, struct sockaddr_in proxyaddr);

  void node_accept_fd(int node, int nodefd, struct sockaddr_in nodeaddr);

  void unregister_proxyfd(int proxyfd);

  bool is_nodefd_alive(int node, int nodefd);

  void clear_nodefds(int node);

private:
  // -1 if not filled
  int last_node, last_nodefd;

  // node --> (proxyfd, addr)
  std::unordered_map<int, std::vector<std::pair<int, struct sockaddr_in>>>
      connecting_proxyfds;

  // (node, nodefd) --> proxyfd
  // use key(node, nodefd) to get required key
  std::unordered_map<size_t, int> nodefd_to_proxyfd;

  // proxyfd --> (node, nodefd)
  std::unordered_map<int, std::pair<int, int>> proxyfd_to_nodefd;

  // node --> nodefd
  std::unordered_map<int, std::unordered_set<int>> dead_nodefds;
};