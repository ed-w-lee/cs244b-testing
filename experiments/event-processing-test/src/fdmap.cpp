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
    connecting_proxyfds[i] = std::vector<std::pair<int, struct sockaddr_in>>();
    dead_nodefds[i] = std::unordered_set<int>();
  }
  for (size_t i = 0; i < num_clients; i++) {
    size_t idx = i + ClientFilter::CLIENT_OFFS;
    connecting_proxyfds[idx] =
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

  dead_nodefds[node].erase(fd);
  last_node = node;
  last_nodefd = fd;
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
  nodefd_to_proxyfd[key(last_node, last_nodefd)] = proxyfd;
  proxyfd_to_nodefd[proxyfd] = {last_node, last_nodefd};

  last_node = -1;
  last_nodefd = -1;
}

void FdMap::proxy_connect_fd(int node, int proxyfd,
                             struct sockaddr_in proxyaddr) {
  printf("[FDMAP] proxy attempting to connect using fd %d with %s:%d\n",
         proxyfd, inet_ntoa(proxyaddr.sin_addr), ntohs(proxyaddr.sin_port));
  connecting_proxyfds[node].push_back({proxyfd, proxyaddr});
}

void FdMap::node_accept_fd(int node, int nodefd, struct sockaddr_in nodeaddr) {
  bool found = false;
  auto it = connecting_proxyfds[node].begin();
  while (it != connecting_proxyfds[node].end()) {
    if (it->second.sin_family == nodeaddr.sin_family &&
        it->second.sin_addr.s_addr == nodeaddr.sin_addr.s_addr &&
        it->second.sin_port == nodeaddr.sin_port) {
      found = true;
      printf("[FDMAP] node accepted %s:%d with fd: %d, corresponding to %d\n",
             inet_ntoa(nodeaddr.sin_addr), ntohs(nodeaddr.sin_port), nodefd,
             it->first);
      nodefd_to_proxyfd[key(node, nodefd)] = it->first;
      proxyfd_to_nodefd[it->first] = {node, nodefd};
      it = connecting_proxyfds[node].erase(it);
      break;
    } else {
      it++;
    }
  }
  if (!found) {
    fprintf(stderr, "[FDMAP] node accepting addr %s:%d with no proxy addr",
            inet_ntoa(nodeaddr.sin_addr), ntohs(nodeaddr.sin_port));
  }
}

std::pair<int, int> FdMap::get_related_nodefd(int proxyfd) {
  return proxyfd_to_nodefd[proxyfd];
}

void FdMap::unregister_proxyfd(int proxyfd) {
  if (proxyfd_to_nodefd.find(proxyfd) != proxyfd_to_nodefd.end()) {
    auto pair = proxyfd_to_nodefd[proxyfd];
    proxyfd_to_nodefd.erase(proxyfd);
    auto my_key = key(pair.first, pair.second);
    if (nodefd_to_proxyfd.find(my_key) != nodefd_to_proxyfd.end() &&
        nodefd_to_proxyfd[my_key] == proxyfd) {
      nodefd_to_proxyfd.erase(key(pair.first, pair.second));
      dead_nodefds[pair.first].insert(pair.second);
      printf("[FDMAP] unregistered %d, moved (%d,%d) to dead\n", proxyfd,
             pair.first, pair.second);
    } else {
      printf("[FDMAP] unregistered %d, did not move (%d,%d) to dead\n", proxyfd,
             pair.first, pair.second);
    }
  }
}

bool FdMap::is_nodefd_alive(int node, int nodefd) {
  if (nodefd_to_proxyfd.find(key(node, nodefd)) != nodefd_to_proxyfd.end()) {
    return true;
  } else if (dead_nodefds[node].find(nodefd) != dead_nodefds[node].end()) {
    return false;
  } else {
    fprintf(stderr, "[FDMAP] node %d nodefd %d not in n_to_p or dead_n\n", node,
            nodefd);
    exit(1);
  }
}

void FdMap::clear_nodefds(int node) {
  // do not unregister from n_to_p or p_to_n since these should be
  // cleared by the proxy already
  connecting_proxyfds[node].clear();
  dead_nodefds[node].clear();
}