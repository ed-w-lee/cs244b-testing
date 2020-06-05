#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

#include <algorithm>
#include <deque>
#include <unordered_map>
#include <vector>

#include "client.h"

#include "fdmap.h"

FdMap::FdMap(size_t num_nodes, size_t num_clients)
    : last_node(-1), last_nodefd(-1), nodefd_to_proxyfd(), proxyfd_to_nodefd() {
  for (size_t i = 0; i < num_nodes; i++) {
    nodes_to_connecting_proxyfds[i] =
        std::vector<std::pair<int, struct sockaddr_in>>();
    dead_nodefds[i] = std::unordered_set<int>();
  }
  for (size_t i = 0; i < num_clients; i++) {
    size_t idx = i + ClientFilter::CLIENT_OFFS;
    nodes_to_connecting_proxyfds[idx] =
        std::vector<std::pair<int, struct sockaddr_in>>();
    dead_nodefds[idx] = std::unordered_set<int>();
  }
}

int FdMap::get_last_node() { return last_node; }

void FdMap::trash_last_node() {
  printf("[FDMAP] trashing (%d,%d)\n", last_node, last_nodefd);
  dead_nodefds[last_node].insert(last_nodefd);
  nodefd_to_proxyfd.erase(key(last_node, last_nodefd));
  last_node = -1;
  last_nodefd = -1;
}

void FdMap::node_connect_fd(int node, int fd) {
  if (last_node != -1 || last_nodefd != -1) {
    fprintf(stderr,
            "[FDMAP] updated connectfd before linking - "
            "last_node: %d, last_fd: %d\n",
            last_node, last_nodefd);
    exit(1);
  }

  printf("[FDMAP] node connecting with (%d,%d)\n", node, fd);
  print_state();

  dead_nodefds[node].erase(fd);
  last_node = node;
  last_nodefd = fd;
  print_state();
}

void FdMap::proxy_accept_fd(int proxyfd) {
  if (last_node == -1 || last_nodefd == -1) {
    fprintf(stderr,
            "[FDMAP] linking proxy with unfilled nodefd - "
            "last_node: %d, last_fd: %d\n",
            last_node, last_nodefd);
    exit(1);
  }

  printf("[FDMAP] proxy accepted (%d,%d) with %d\n", last_node, last_nodefd,
         proxyfd);
  print_state();
  dead_proxyfds.erase(proxyfd);

  nodefd_to_proxyfd[key(last_node, last_nodefd)] = proxyfd;
  proxyfd_to_nodefd[proxyfd] = {last_node, last_nodefd};

  last_node = -1;
  last_nodefd = -1;
  print_state();
}

void FdMap::proxy_connect_fd(int node, int proxyfd,
                             struct sockaddr_in proxyaddr) {
  printf("[FDMAP] proxy attempting to connect using fd %d with %s:%d\n",
         proxyfd, inet_ntoa(proxyaddr.sin_addr), ntohs(proxyaddr.sin_port));
  print_state();
  dead_proxyfds.erase(proxyfd);
  connecting_proxyfds[proxyfd] = node;

  nodes_to_connecting_proxyfds[node].push_back({proxyfd, proxyaddr});
  print_state();
}

void FdMap::node_accept_fd(int node, int nodefd, struct sockaddr_in nodeaddr) {
  bool found = false;
  auto it = nodes_to_connecting_proxyfds[node].begin();
  while (it != nodes_to_connecting_proxyfds[node].end()) {
    if (it->second.sin_family == nodeaddr.sin_family &&
        it->second.sin_addr.s_addr == nodeaddr.sin_addr.s_addr &&
        it->second.sin_port == nodeaddr.sin_port) {
      found = true;
      printf("[FDMAP] node accepted %s:%d with fd: %d, corresponding to %d\n",
             inet_ntoa(nodeaddr.sin_addr), ntohs(nodeaddr.sin_port), nodefd,
             it->first);
      print_state();

      dead_nodefds[node].erase(nodefd);
      connecting_proxyfds.erase(it->first);

      nodefd_to_proxyfd[key(node, nodefd)] = it->first;
      proxyfd_to_nodefd[it->first] = {node, nodefd};

      it = nodes_to_connecting_proxyfds[node].erase(it);
      print_state();
      break;
    } else {
      it++;
    }
  }
  if (!found) {
    fprintf(stderr, "[FDMAP] node accepting addr %s:%d with no proxy addr",
            inet_ntoa(nodeaddr.sin_addr), ntohs(nodeaddr.sin_port));
    exit(1);
  }
}

void FdMap::node_close_fd(int node, int nodefd) {
  // assume that node won't close an fd that's connecting
  size_t my_key = key(node, nodefd);
  printf("[FDMAP] closing (%d, %d)\n", node, nodefd);
  print_state();
  if (nodefd_to_proxyfd.find(my_key) != nodefd_to_proxyfd.end()) {
    // closing something that hasn't been unregistered by proxy
    int proxyfd = nodefd_to_proxyfd.at(key(node, nodefd));
    printf("[FDMAP] successfully mapped to %d\n", proxyfd);
    nodefd_to_proxyfd.erase(key(node, nodefd));
    proxyfd_to_nodefd.erase(proxyfd);

    dead_nodefds[node].insert(nodefd);
    dead_proxyfds.insert(proxyfd);
  }
  print_state();
}

void FdMap::proxy_clear_connecting(int proxyfd) {
  printf("[FDMAP] Clearing connecting\n");
  print_state();
  if (connecting_proxyfds.find(proxyfd) != connecting_proxyfds.end()) {
    int node = connecting_proxyfds[proxyfd];
    connecting_proxyfds.erase(proxyfd);
    auto it = nodes_to_connecting_proxyfds[node].begin();
    while (it != nodes_to_connecting_proxyfds[node].end()) {
      if (it->first == proxyfd) {
        it = nodes_to_connecting_proxyfds[node].erase(it);
        break;
      } else {
        it++;
      }
    }
  }
  print_state();
}

std::pair<int, int> FdMap::get_related_nodefd(int proxyfd) {
  return proxyfd_to_nodefd.at(proxyfd);
}

bool FdMap::is_linked(int proxyfd) {
  // proxyfd must be linked and not dead
  return (proxyfd_to_nodefd.find(proxyfd) != proxyfd_to_nodefd.end() &&
          dead_proxyfds.find(proxyfd) == dead_proxyfds.end());
}

void FdMap::unregister_proxyfd(int proxyfd) {
  printf("[FDMAP] unregistering proxyfd %d\n", proxyfd);
  print_state();
  dead_proxyfds.insert(proxyfd);
  if (proxyfd_to_nodefd.find(proxyfd) != proxyfd_to_nodefd.end()) {
    auto pair = proxyfd_to_nodefd[proxyfd];
    proxyfd_to_nodefd.erase(proxyfd);
    auto my_key = key(pair.first, pair.second);
    if (nodefd_to_proxyfd.find(my_key) != nodefd_to_proxyfd.end() &&
        nodefd_to_proxyfd[my_key] == proxyfd) {
      // nodefd wasn't closed, let's kill it
      nodefd_to_proxyfd.erase(key(pair.first, pair.second));
      dead_nodefds[pair.first].insert(pair.second);
      printf("[FDMAP] unregistered, moved (%d,%d) to dead\n", pair.first,
             pair.second);
    } else {
      printf("[FDMAP] unregistered, did not move (%d,%d) to dead\n", pair.first,
             pair.second);
    }
  }
  print_state();
}

bool FdMap::is_nodefd_alive(int node, int nodefd) {
  if (node == last_node && nodefd == last_nodefd) {
    // nodefd is currently connecting
    return true;
  } else if (nodefd_to_proxyfd.find(key(node, nodefd)) !=
             nodefd_to_proxyfd.end()) {
    // nodefd is linked
    return true;
  } else if (dead_nodefds[node].find(nodefd) != dead_nodefds[node].end()) {
    // nodefd is dead
    return false;
  } else {
    // unknown state
    fprintf(stderr, "[FDMAP] node %d nodefd %d in unknown state\n", node,
            nodefd);
    exit(1);
  }
}

void FdMap::clear_nodefds(int node) {
  // do not unregister from n_to_p or p_to_n since these should be
  // cleared by the proxy already
  printf("[FDMAP] clearing nodefds of %d\n", node);
  print_state();
  nodes_to_connecting_proxyfds[node].clear();
  dead_nodefds[node].clear();
  print_state();
}

void FdMap::print_state() {
  if (true) {
    return;
  }
  printf("[FDMAP STATE] connecting_proxyfds:\n");
  for (const auto &tup : connecting_proxyfds) {
    printf("[FDMAP STATE]  %d -> %d\n", tup.first, tup.second);
  }
  printf("[FDMAP STATE] proxyfd_to_nodefd\n");
  for (const auto &tup : proxyfd_to_nodefd) {
    printf("[FDMAP STATE]  %d -> (%d, %d)\n", tup.first, tup.second.first,
           tup.second.second);
  }
  printf("[FDMAP STATE] dead_proxyfds\n");
  for (const auto &tup : dead_proxyfds) {
    printf("[FDMAP STATE]  %d\n", tup);
  }

  printf("[FDMAP STATE] last_node %d + last_nodefd %d\n", last_node,
         last_nodefd);
  printf("[FDMAP STATE] nodefd_to_proxyfd\n");
  for (const auto &tup : nodefd_to_proxyfd) {
    int node = node_from_key(tup.first);
    int fd = fd_from_key(tup.first);
    printf("[FDMAP STATE]  (%d, %d) -> %d\n", node, fd, tup.second);
  }
  printf("[FDMAP STATE] dead_nodefds\n");
  for (const auto &tup : dead_nodefds) {
    printf("[FDMAP STATE]  %d -> ", tup.first);
    for (const auto &fd : tup.second) {
      printf("%d ", fd);
    }
    printf("\n");
  }
}