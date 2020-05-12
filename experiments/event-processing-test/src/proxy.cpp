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

#include <deque>
#include <unordered_map>
#include <vector>

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
}

} // namespace

Proxy::Proxy(sockaddr_in my_addr, sockaddr_in node_addr,
             std::vector<sockaddr_in> actual_node_map,
             std::vector<sockaddr_in> proxy_node_map)
    : node_alive(true), node_addr(node_addr), actual_node_map(actual_node_map),
      proxy_node_map(proxy_node_map) {
  printf("[PROXY] initialize\n");

  efd = epoll_create1(EPOLL_CLOEXEC);

  sockfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
  if (bind(sockfd, (const sockaddr *)&my_addr, sizeof(sockaddr_in)) < 0) {
    fprintf(stderr, "[PROXY] bind failed: %s\n", strerror(errno));
    exit(1);
  }

  _set_nonblocking(sockfd);
  _set_reuseaddr(sockfd);
  if (listen(sockfd, 100) < 0) {
    fprintf(stderr, "[PROXY] listen failed: %s\n", strerror(errno));
  }

  // // we don't care about the port for when we're connecting as a proxy node
  // for (auto &&proxy_node : proxy_node_map) {
  //   proxy_node.sin_port = htons(0);
  // }

  register_fd(sockfd);
}

void Proxy::toggle_node() {
  if (node_alive) {
    stop_node();
  } else {
    node_alive = true;
  }
}

void Proxy::stop_node() {
  std::unordered_set<int> tmpfds(inbound_fds.begin(), inbound_fds.end());
  for (int fd : tmpfds) {
    unregister_fd(fd);
  }
  node_alive = false;
}

const std::unordered_map<int, std::deque<std::vector<char>>> &
Proxy::get_msgs() {
  // find all messages currently available
  while (poll_for_events())
    ;
  return waiting_msgs;
}

void Proxy::allow_next_msg(int fd) {
  printf("[PROXY] sending next message for fd: %d\n", fd);
  auto got = waiting_msgs.find(fd);
  if (got == waiting_msgs.end()) {
    printf("[PROXY] no messages for fd: %d can be sent\n", fd);
  } else {
    std::vector<char> mesg = got->second.front();
    got->second.pop_front();
    size_t msg_len = mesg.size();
    // TODO - think about partial sends
    if (sendto(got->first, &mesg[0], msg_len, 0, nullptr, 0) < (int)msg_len) {
      fprintf(stderr, "[PROXY] couldn't send everything at once\n");
      exit(1);
    }
    if (got->second.empty() && related_fd[fd] < 0) {
      unregister_fd(fd);
    }
  }
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

void Proxy::unregister_fd(int fd) {
  printf("[PROXY] unregistering %d\n", fd);
  epoll_ctl(efd, EPOLL_CTL_DEL, fd, nullptr);
  close(fd);
  if (inbound_fds.find(fd) != inbound_fds.end()) {
    inbound_fds.erase(fd);
  }
  if (fd_to_node.find(fd) != fd_to_node.end()) {
    fd_to_node.erase(fd);
  }
  waiting_msgs.erase(fd);
  // unlink related fds
  printf("[PROXY] relatedfd[%d]=%d\n", fd, related_fd[fd]);
  if (related_fd[fd] >= 0) {
    int other_fd = related_fd[fd];
    related_fd[other_fd] = -1;

    if (waiting_msgs[other_fd].empty()) {
      unregister_fd(other_fd);
    }
  }
  related_fd.erase(fd);
}

bool Proxy::poll_for_events() {
  const size_t NUM_EVENTS = 100;
  const size_t MAX_BUF = 2000;
  struct epoll_event evs[NUM_EVENTS];
  bool something_occurred = false;

  int num_events = epoll_wait(efd, (epoll_event *)&evs, NUM_EVENTS, 0);
  for (int i = 0; i < num_events; i++) {
    if (evs[i].events & EPOLLIN) {
      if ((int)evs[i].data.u32 == sockfd) {
        int idx = 0, fromfd, peerfd;
        bool found;
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
          if (!node_alive) {
            close(fromfd);
            continue;
          }
          printf("[PROXY] accepted connection from: %s:%d\n",
                 inet_ntoa(new_conn.sin_addr), ntohs(new_conn.sin_port));
          // register new node if peer
          found = false;
          for (auto &addr : actual_node_map) {
            // TODO would be nice to handle own node
            if (addr.sin_addr.s_addr == new_conn.sin_addr.s_addr) {
              printf("[PROXY] found node at %d\n", idx);
              found = true;
              fd_to_node[fromfd] = idx;
              break;
            }
            idx++;
          }
          _set_nonblocking(fromfd);
          _set_reuseaddr(fromfd);
          register_fd(fromfd);
          waiting_msgs[fromfd] = std::deque<std::vector<char>>();
        }

        {
          // create connection with node as proxy
          peerfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);

          if (found) {
            sockaddr_in x = proxy_node_map[idx];
            x.sin_port = htons(0);
            printf("[PROXY] binding to proxy_node_map[%d]: %s:%d\n", idx,
                   inet_ntoa(x.sin_addr), ntohs(x.sin_port));
            if (bind(peerfd, (const sockaddr *)&x, sizeof(sockaddr_in)) < 0) {
              fprintf(stderr, "[PROXY] bind failed: %s\n", strerror(errno));
              exit(1);
            }
          }
          if (connect(peerfd, (const sockaddr *)&node_addr,
                      sizeof(sockaddr_in)) < 0) {
            fprintf(stderr, "[PROXY] connect to %s failed: %s\n",
                    inet_ntoa(node_addr.sin_addr), strerror(errno));
            close(peerfd);
            unregister_fd(fromfd);
            continue;
          }
          _set_nonblocking(peerfd);
          _set_reuseaddr(peerfd);
          register_fd(peerfd);
          waiting_msgs[peerfd] = std::deque<std::vector<char>>();
          inbound_fds.insert(peerfd);
        }

        link_fds(peerfd, fromfd);
        something_occurred = true;
      } else {
        int conn_fd = (int)evs[i].data.u32;
        if (related_fd.find(conn_fd) == related_fd.end())
          // unregistered already, so just ignore it
          continue;
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
  printf("[PROXY STATE] %s state:\n", inet_ntoa(node_addr.sin_addr));
  for (auto &x : waiting_msgs) {
    auto it = fd_to_node.find(x.first);
    int node = it == fd_to_node.end() ? -1 : it->second;
    printf("[PROXY STATE] fd %2d --> %2d: [", x.first, node);
    for (auto &v : x.second) {
      printf("%c%c, ", v[0], v[1]); // hack to get a "hash" the message
    }
    printf("]\n");
  }
}