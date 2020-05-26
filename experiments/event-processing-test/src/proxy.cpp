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
#include "proxy.h"

namespace {
void _set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    fprintf(stderr, "[PROXY] fcntl failed: %s\n", strerror(errno));
    exit(1);
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    fprintf(stderr, "[PROXY] fcntl failed: %s\n", strerror(errno));
    exit(1);
  }
}

void _set_reuseaddr(int fd) {
  int enable = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    fprintf(stderr, "[PROXY] REUSEADDR failed: %s\n", strerror(errno));
    exit(1);
  }
  // if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) {
  //   fprintf(stderr, "[PROXY] REUSEPORT failed: %s\n", strerror(errno));
  //   exit(1);
  // }
  struct linger lin;
  lin.l_onoff = 0;
  lin.l_linger = 0;
  if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(struct linger)) < 0) {
    fprintf(stderr, "[PROXY] LINGER failed: %s\n", strerror(errno));
    exit(1);
  }
}

} // namespace

Proxy::Proxy(FdMap &fdmap, std::vector<sockaddr_in> actual_node_map,
             std::vector<sockaddr_in> proxy_node_map, size_t num_clients)
    : actual_node_map(actual_node_map), proxy_node_map(proxy_node_map),
      fdmap(fdmap) {
  printf("[PROXY] initialize\n");

  sockfds.reserve(actual_node_map.size());

  efd = epoll_create1(EPOLL_CLOEXEC);

  for (size_t i = 0; i < actual_node_map.size(); i++) {
    int sockfd = create_listen(i);
    sockfds.push_back(sockfd);

    node_alive[i] = false;
    inbound_fds[i] = std::unordered_set<int>();
  }
  for (size_t i = 0; i < num_clients; i++) {
    inbound_fds[ClientFilter::CLIENT_OFFS + i] = std::unordered_set<int>();
    node_alive[ClientFilter::CLIENT_OFFS + i] = false;
  }
}

void Proxy::set_alive(int idx) { node_alive[idx] = true; }

std::vector<std::pair<int, int>> Proxy::toggle_node(int idx) {
  if (node_alive[idx]) {
    poll_for_events(false);
    return stop_node(idx);
  } else {
    node_alive[idx] = true;
    if (idx < ClientFilter::CLIENT_OFFS) {
      int sockfd = create_listen(idx);
      sockfds[idx] = sockfd;
    }
    return {};
  }
}

int Proxy::create_listen(int idx) {
  int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
  if (sockfd < 0) {
    fprintf(stderr, "[PROXY] socket failed: %s\n", strerror(errno));
    exit(1);
  }
  printf("[PROXY] creating listening sockfd: %d\n", sockfd);
  _set_nonblocking(sockfd);
  _set_reuseaddr(sockfd);
  if (bind(sockfd, (const sockaddr *)&proxy_node_map[idx],
           sizeof(sockaddr_in)) < 0) {
    fprintf(stderr, "[PROXY] bind failed: %s\n", strerror(errno));
    exit(1);
  }
  if (listen(sockfd, 100) < 0) {
    fprintf(stderr, "[PROXY] listen failed: %s\n", strerror(errno));
    exit(1);
  }

  register_fd(sockfd);
  return sockfd;
}

std::vector<std::pair<int, int>> Proxy::stop_node(int idx) {
  std::unordered_set<int> tmpfds(inbound_fds[idx].begin(),
                                 inbound_fds[idx].end());
  std::vector<std::pair<int, int>> related_nodes;
  for (int fd : tmpfds) {
    unregister_fd(fd, &related_nodes);
  }
  node_alive[idx] = false;
  if (idx < ClientFilter::CLIENT_OFFS) {
    epoll_ctl(efd, EPOLL_CTL_DEL, sockfds[idx], nullptr);
    printf("[PROXY] closing listen fd %d for: %d\n", sockfds[idx], idx);
    close(sockfds[idx]);
    sockfds[idx] = -1;
  }
  return related_nodes;
}

std::vector<int> Proxy::get_fds_with_msgs(int idx) {
  std::vector<int> to_ret;
  for (int fd : inbound_fds[idx]) {
    if (waiting_msgs[fd].size() > 0) {
      to_ret.push_back(fd);
    }
  }
  return to_ret;
}

bool Proxy::has_more(int fd) {
  auto got = waiting_msgs.find(fd);
  if (got == waiting_msgs.end()) {
    return false;
  } else {
    return !got->second.empty();
  }
}

bool Proxy::allow_next_msg(int fd) {
  printf("[PROXY] sending next message for fd: %d\n", fd);
  auto got = waiting_msgs.find(fd);
  if (got == waiting_msgs.end()) {
    printf("[PROXY] could not find fd %d in waiting_msgs\n", fd);
  } else {
    std::vector<char> mesg = got->second.front();
    got->second.pop_front();
    size_t msg_len = mesg.size();
    // TODO - think about partial sends
    if (sendto(got->first, &mesg[0], msg_len, 0, nullptr, 0) < (int)msg_len) {
      fprintf(stderr, "[PROXY] couldn't send everything or send failed\n");
      exit(1);
    }
    if (got->second.empty() && related_fd[fd] < 0) {
      // if the client has closed, then we should close the connection with the
      // node
      unregister_fd(fd);
      return true;
    }
  }
  return false;
}

void Proxy::register_fd(int fd) {
  printf("[PROXY] registering %d\n", fd);
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLRDHUP;
  ev.data.u32 = fd;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) < 0) {
    fprintf(stderr, "[PROXY] epoll_ctl_add failed: %s\n", strerror(errno));
    exit(1);
  }
}

void Proxy::link_fds(int fd1, int fd2) {
  printf("[PROXY] linking %d and %d\n", fd1, fd2);
  related_fd[fd1] = fd2;
  related_fd[fd2] = fd1;
}

void Proxy::unregister_fd(int fd,
                          std::vector<std::pair<int, int>> *related_nodes) {
  printf("[PROXY] unregistering %d\n", fd);
  fdmap.unregister_proxyfd(fd);
  epoll_ctl(efd, EPOLL_CTL_DEL, fd, nullptr);
  close(fd);
  auto it = fd_to_node.find(fd);
  if (it != fd_to_node.end()) {
    int idx = it->second;
    if (inbound_fds[idx].find(fd) != inbound_fds[idx].end()) {
      inbound_fds[idx].erase(fd);
    }
    fd_to_node.erase(fd);
  }
  waiting_msgs.erase(fd);
  // unlink related fds
  if (related_fd.find(fd) != related_fd.end()) {
    printf("[PROXY] relatedfd[%d]=%d\n", fd, related_fd[fd]);
    if (related_fd[fd] >= 0) {
      int other_fd = related_fd[fd];
      related_fd[other_fd] = -1;

      if (waiting_msgs[other_fd].empty()) {
        if (related_nodes != nullptr) {
          auto tup = fdmap.get_related_nodefd(other_fd);
          printf("[PROXY] add (%d, %d) to related_nodes\n", tup.first,
                 tup.second);
          related_nodes->push_back(tup);
        }
        unregister_fd(other_fd);
      }
    }
    related_fd.erase(fd);
  }
}

bool Proxy::poll_for_events(bool blocking) {
  const size_t NUM_EVENTS = 100;
  const size_t MAX_BUF = 2000;
  struct epoll_event evs[NUM_EVENTS];
  bool something_occurred = false;

  int num_events =
      epoll_wait(efd, (epoll_event *)&evs, NUM_EVENTS, blocking ? -1 : 0);
  printf("[PROXY] found %d events\n", num_events);
  for (int i = 0; i < num_events; i++) {
    if (evs[i].events & EPOLLIN) {
      printf("[PROXY] input event\n");
      const auto &it =
          std::find(sockfds.begin(), sockfds.end(), (int)evs[i].data.u32);
      if (it != sockfds.end()) {
        printf("[PROXY] new node connection\n");
        int my_idx = std::distance(sockfds.begin(), it);
        int sockfd = *it;
        int idx = 0, fromfd, peerfd;
        bool found;
        sockaddr_in my_addr;
        {
          // can accept new connection
          sockaddr_in new_conn;
          socklen_t len = sizeof(sockaddr_in);
          fromfd =
              accept4(sockfd, (struct sockaddr *)&new_conn, &len, SOCK_CLOEXEC);
          if (fromfd < 0) {
            fprintf(stderr, "[PROXY] accept failed: %s\n", strerror(errno));
            exit(1);
          }
          // register new node if peer
          printf("[PROXY] accepted connection for %d from: %s:%d\n", my_idx,
                 inet_ntoa(new_conn.sin_addr), ntohs(new_conn.sin_port));

          if (!node_alive[my_idx] || *it == -1) {
            fprintf(stderr, "[PROXY] node is not alive, sockfd: %d\n", *it);
            unregister_fd(fromfd);
            fdmap.trash_last_node();
            continue;
          }

          idx = fdmap.get_last_node();
          if (idx == -1) {
            fprintf(stderr, "[PROXY] idx of last node invalid\n");
            exit(1);
          }
          found = idx < ClientFilter::CLIENT_OFFS;
          fd_to_node[fromfd] = idx;
          inbound_fds[idx].insert(fromfd);
          _set_nonblocking(fromfd);
          _set_reuseaddr(fromfd);
          register_fd(fromfd);
          waiting_msgs[fromfd] = std::deque<std::vector<char>>();
        }

        {
          // create connection with node as proxy
          peerfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);

          if (found) {
            my_addr = proxy_node_map[idx];
            my_addr.sin_port = htons(0);
            printf("[PROXY] binding to proxy_node_map[%d]: %s:%d\n", idx,
                   inet_ntoa(my_addr.sin_addr), ntohs(my_addr.sin_port));
            if (bind(peerfd, (const sockaddr *)&my_addr, sizeof(sockaddr_in)) <
                0) {
              fprintf(stderr, "[PROXY] bind failed: %s\n", strerror(errno));
              exit(1);
            }
          } else {
            my_addr.sin_family = AF_INET;
            my_addr.sin_port = htons(0);
            if (inet_aton("127.0.0.1", &(my_addr.sin_addr)) <= 0) {
              fprintf(stderr, "[PROXY] localhost inet_aton failed: %s\n",
                      strerror(errno));
              exit(1);
            }
            if (bind(peerfd, (const sockaddr *)&my_addr, sizeof(sockaddr_in)) <
                0) {
              fprintf(stderr, "[PROXY] bind failed: %s\n", strerror(errno));
              exit(1);
            }
          }
          if (connect(peerfd, (const sockaddr *)&actual_node_map[my_idx],
                      sizeof(sockaddr_in)) < 0) {
            fprintf(stderr, "[PROXY] connect to %s failed: %s\n",
                    inet_ntoa(actual_node_map[my_idx].sin_addr),
                    strerror(errno));
            unregister_fd(fromfd);
            fdmap.trash_last_node();
            continue;
          }
          _set_nonblocking(peerfd);
          _set_reuseaddr(peerfd);
          register_fd(peerfd);
          waiting_msgs[peerfd] = std::deque<std::vector<char>>();
          fd_to_node[peerfd] = my_idx;
          inbound_fds[my_idx].insert(peerfd);
        }

        link_fds(peerfd, fromfd);

        socklen_t addrlen = sizeof(sockaddr_in);
        getsockname(peerfd, (sockaddr *)&my_addr, &addrlen);
        fdmap.proxy_accept_fd(fromfd);
        fdmap.proxy_connect_fd(my_idx, peerfd, my_addr);

        something_occurred = true;
      } else {
        printf("[PROXY] receiving message\n");
        int conn_fd = (int)evs[i].data.u32;
        if (related_fd.find(conn_fd) == related_fd.end()) {
          // unregistered already, so just ignore it
          printf("[PROXY] message received from unregistered fd\n");
          continue;
        }
        char buf[MAX_BUF];
        ssize_t n_bytes =
            recvfrom(conn_fd, &buf, MAX_BUF - 1, 0, nullptr, nullptr);
        if (n_bytes < 0) {
          fprintf(stderr, "[PROXY] recv failed: %s\n", strerror(errno));
          unregister_fd(conn_fd);
        } else if (n_bytes == 0) {
          printf("[PROXY] nothing read, closing %d\n", conn_fd);
          // assume later than Linux 2.6.9
          unregister_fd(conn_fd);
        } else {
          buf[n_bytes] = 0;
          std::vector<char> mesg(buf, buf + n_bytes);
          printf("[PROXY] read: %s\n", buf);

          if (related_fd[conn_fd] >= 0) {
            printf("[PROXY] adding to waiting_msgs\n");
            waiting_msgs[related_fd[conn_fd]].push_back(mesg);
            printf("[PROXY] new queue len: %lu\n",
                   waiting_msgs[related_fd[conn_fd]].size());
          } else {
            printf("[PROXY] no related fd, not adding to waiting msgs\n");
          }
        }
        something_occurred = true;
      }
    }
    if (evs[i].events & (EPOLLHUP | EPOLLRDHUP)) {
      int conn_fd = (int)evs[i].data.u32;
      // unregistered already, so just ignore it
      if (related_fd.find(conn_fd) == related_fd.end())
        continue;
      // assume later than Linux 2.6.9
      unregister_fd(conn_fd);
    }
  }
  return something_occurred;
}

void Proxy::print_state() {
  printf("[PROXY STATE] waiting_msgs:\n");
  for (auto &x : waiting_msgs) {
    int node =
        fd_to_node.find(x.first) == fd_to_node.end() ? -1 : fd_to_node[x.first];
    printf("[PROXY STATE] fd %2d --> %4d: [", x.first, node);
    for (auto &v : x.second) {
      printf("(%c%c, %lu), ", v[0], v[1], v.size());
    }
    printf("]\n");
  }
}