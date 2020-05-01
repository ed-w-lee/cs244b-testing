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

Proxy::Proxy(sockaddr_in my_addr, sockaddr_in node_addr,
             std::vector<sockaddr_in> node_map)
    : node_addr(node_addr), node_map(node_map), waiting_msgs() {
  printf("initialize\n");

  efd = epoll_create1(EPOLL_CLOEXEC);

  sockfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
  if (bind(sockfd, (const sockaddr *)&my_addr, sizeof(sockaddr_in)) < 0) {
    fprintf(stderr, "bind failed: %s\n", strerror(errno));
    exit(1);
  }

  int flags = fcntl(sockfd, F_GETFL, 0);
  fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
  int enable = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
  if (listen(sockfd, 100) < 0) {
    fprintf(stderr, "listen failed: %s\n", strerror(errno));
  }

  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLRDHUP;
  ev.data.u32 = sockfd;
  epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &ev);
}

const std::unordered_map<int, std::queue<std::string>> &Proxy::get_msgs() {
  // poll to see what needs to get read and read them, i guess
  poll_for_events();
  return waiting_msgs;
}

void Proxy::poll_for_events() {
  const size_t NUM_EVENTS = 100;
  const size_t MAX_BUF = 2000;
  struct epoll_event evs[NUM_EVENTS];
  int num_events = epoll_wait(efd, (struct epoll_event *)&evs, NUM_EVENTS, 0);
  for (int i = 0; i < num_events; i++) {
    if (evs[i].events & EPOLLIN) {
      if ((int)evs[i].data.u32 == sockfd) {
        // can accept new connection
        sockaddr_in new_conn;
        socklen_t len = sizeof(sockaddr_in);
        int new_fd =
            accept4(sockfd, (struct sockaddr *)&new_conn, &len, SOCK_CLOEXEC);
        if (new_fd < 0) {
          fprintf(stderr, "%s\n", strerror(errno));
          exit(1);
        }
        // register new node if peer
        int i = 0;
        bool found = false;
        for (auto &addr : node_map) {
          // TODO would be nice to handle own node
          if (addr.sin_addr.s_addr == new_conn.sin_addr.s_addr) {
            found = true;
            fd_to_node[new_fd] = i;
            waiting_msgs[i] = std::queue<std::string>();
            break;
          }
          i++;
        }
        if (!found) {
          waiting_msgs[new_fd + CLIENT_OFFS] = std::queue<std::string>();
        }

        // set nonblocking
        int flags = fcntl(new_fd, F_GETFL, 0);
        fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
        // int enable = 1;
        // setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLHUP;
        ev.data.u32 = new_fd;
        epoll_ctl(efd, EPOLL_CTL_ADD, new_fd, &ev);

        // TODO - create connection with node
        int peerfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
        flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
        if (connect(peerfd, (const sockaddr *)&node_addr, sizeof(sockaddr_in)) <
            0) {
          fprintf(stderr, "%s\n", strerror(errno));
          exit(1);
        }

        ev.events = EPOLLIN | EPOLLHUP;
        ev.data.u32 = new_fd;
        epoll_ctl(efd, EPOLL_CTL_ADD, new_fd, &ev);
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
          int conn_fd = (int)evs[i].data.u32;
          printf("nothing read, closing %d", conn_fd);
          // assume later than Linux 2.6.9
          epoll_ctl(efd, EPOLL_CTL_DEL, conn_fd, nullptr);
          close(conn_fd);
        } else {
          buf[n_bytes] = 0;
          std::string mesg(buf);
          printf("read: %s\n", buf);

          auto got = fd_to_node.find(conn_fd);
          if (got == fd_to_node.end()) {
            // client
            waiting_msgs[conn_fd + CLIENT_OFFS].push(mesg);
          } else {
            waiting_msgs[got->second].push(mesg);
          }
        }
      }
    }
    if (evs[i].events & (EPOLLHUP | EPOLLRDHUP)) {
      int conn_fd = (int)evs[i].data.u32;
      printf("closing %d", conn_fd);
      // assume later than Linux 2.6.9
      epoll_ctl(efd, EPOLL_CTL_DEL, conn_fd, nullptr);
      close(conn_fd);
    }
  }
}