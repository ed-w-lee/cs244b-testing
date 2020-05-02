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

#include <queue>
#include <unordered_map>

#include "proxy.h"

namespace {
void _set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    fprintf(stderr, "fcntl failed: %s\n", strerror(errno));
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    fprintf(stderr, "fcntl failed: %s\n", strerror(errno));
  }
}

void _set_reuseaddr(int fd) {
  int enable = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    fprintf(stderr, "REUSEADDR failed: %s\n", strerror(errno));
  }
}

} // namespace

Proxy::Proxy(sockaddr_in my_addr, sockaddr_in node_addr,
             std::vector<sockaddr_in> actual_node_map,
             std::vector<sockaddr_in> proxy_node_map)
    : node_addr(node_addr), actual_node_map(actual_node_map),
      proxy_node_map(proxy_node_map.begin(), proxy_node_map.end()),
      related_fd(), waiting_msgs() {
  printf("initialize\n");

  efd = epoll_create1(EPOLL_CLOEXEC);

  sockfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
  if (bind(sockfd, (const sockaddr *)&my_addr, sizeof(sockaddr_in)) < 0) {
    fprintf(stderr, "bind failed: %s\n", strerror(errno));
    exit(1);
  }

  _set_nonblocking(sockfd);
  _set_reuseaddr(sockfd);
  if (listen(sockfd, 100) < 0) {
    fprintf(stderr, "listen failed: %s\n", strerror(errno));
  }

  // // we don't care about the port for when we're connecting as a proxy node
  // for (auto &&proxy_node : proxy_node_map) {
  //   proxy_node.sin_port = htons(0);
  // }

  register_fd(sockfd);
}

const std::unordered_map<int, std::queue<std::vector<char>>> &
Proxy::get_msgs() {
  // poll to see what needs to get read and read them, i guess
  poll_for_events();
  return waiting_msgs;
}

void Proxy::allow_next_msg(int fd) {
  printf("sending next message for fd: %d\n", fd);
  auto got = waiting_msgs.find(fd);
  if (got == waiting_msgs.end()) {
    printf("no messages for fd: %d can be sent\n", fd);
  } else {
    std::vector<char> mesg = got->second.front();
    got->second.pop();
    size_t msg_len = mesg.size();
    // TODO - think about partial sends
    if (sendto(got->first, &mesg[0], msg_len, 0, nullptr, 0) < (int)msg_len) {
      fprintf(stderr, "couldn't send everything at once\n");
      exit(1);
    }
    if (got->second.empty() && related_fd[fd] < 0) {
      unregister_fd(fd);
    }
  }
}

void Proxy::register_fd(int fd) {
  printf("registering %d\n", fd);
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLRDHUP;
  ev.data.u32 = fd;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) < 0) {
    fprintf(stderr, "epoll_ctl_add failed: %s\n", strerror(errno));
  }
}

void Proxy::link_fds(int fd1, int fd2) {
  printf("linking %d and %d\n", fd1, fd2);
  related_fd[fd1] = fd2;
  related_fd[fd2] = fd1;
}

void Proxy::unregister_fd(int fd) {
  printf("unregistering %d\n", fd);
  epoll_ctl(efd, EPOLL_CTL_DEL, fd, nullptr);
  close(fd);
  // unlink related fds
  printf("relatedfd[%d]=%d\n", fd, related_fd[fd]);
  if (related_fd[fd] >= 0) {
    int other_fd = related_fd[fd];
    related_fd[other_fd] = -1;

    if (waiting_msgs[other_fd].empty()) {
      unregister_fd(other_fd);
    }
  }
  related_fd.erase(fd);
}

void Proxy::poll_for_events() {
  const size_t NUM_EVENTS = 100;
  const size_t MAX_BUF = 2000;
  struct epoll_event evs[NUM_EVENTS];
  int num_events = epoll_wait(efd, (epoll_event *)&evs, NUM_EVENTS, 0);
  for (int i = 0; i < num_events; i++) {
    bool closed = false;
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
            fprintf(stderr, "accept failed: %s\n", strerror(errno));
            exit(1);
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
          waiting_msgs[fromfd] = std::queue<std::vector<char>>();
        }

        {
          // create connection with node as proxy
          peerfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);

          if (found) {
            sockaddr_in x = proxy_node_map[idx];
            x.sin_port = htons(0);
            printf("binding to proxy_node_map[%d]: %s:%d\n", idx,
                   inet_ntoa(x.sin_addr), ntohs(x.sin_port));
            if (bind(peerfd, (const sockaddr *)&x, sizeof(sockaddr_in)) < 0) {
              fprintf(stderr, "bind failed: %s\n", strerror(errno));
              exit(1);
            }
          }
          if (connect(peerfd, (const sockaddr *)&node_addr,
                      sizeof(sockaddr_in)) < 0) {
            fprintf(stderr, "connect failed: %s\n", strerror(errno));
            exit(1);
          }
          _set_nonblocking(peerfd);
          _set_reuseaddr(peerfd);
          register_fd(peerfd);
          waiting_msgs[peerfd] = std::queue<std::vector<char>>();
        }

        link_fds(peerfd, fromfd);
      } else {
        int conn_fd = (int)evs[i].data.u32;
        char buf[MAX_BUF];
        ssize_t n_bytes =
            recvfrom(conn_fd, &buf, MAX_BUF - 1, 0, nullptr, nullptr);
        if (n_bytes < 0) {
          fprintf(stderr, "%s\n", strerror(errno));
          exit(1);
        }
        if (n_bytes == 0) {
          printf("[PROXY] nothing read, closing %d\n", conn_fd);
          // assume later than Linux 2.6.9
          unregister_fd(conn_fd);
          closed = true;
        } else {
          buf[n_bytes] = 0;
          std::vector<char> mesg(buf, buf + n_bytes);
          printf("[PROXY] read: %s\n", buf);

          if (related_fd[conn_fd] >= 0) {
            printf("adding to waiting_msgs\n");
            waiting_msgs[related_fd[conn_fd]].push(mesg);
            printf("[PROXY] new queue len: %lu\n",
                   waiting_msgs[related_fd[conn_fd]].size());
          } else {
            printf("no related fd, not adding to waiting msgs\n");
          }
        }
      }
    }
    if (!closed && evs[i].events & (EPOLLHUP | EPOLLRDHUP)) {
      int conn_fd = (int)evs[i].data.u32;
      // assume later than Linux 2.6.9
      unregister_fd(conn_fd);
    }
  }
}
