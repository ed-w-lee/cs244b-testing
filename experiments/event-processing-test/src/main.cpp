#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/limits.h>
#include <linux/seccomp.h>
#include <linux/unistd.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "filter.h"
#include "proxy.h"

using namespace Filter;

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <seed> <prog> <arg1> ... <argN>\n", argv[0]);
    return 1;
  }

  std::string str_seed(argv[1]);
  std::seed_seq seed(str_seed.begin(), str_seed.end());
  std::mt19937 rng(seed);

  const int NUM_NODES = 3;
  sockaddr_in oldaddrs[NUM_NODES];
  for (int i = 0; i < NUM_NODES; i++) {
    oldaddrs[i].sin_family = AF_INET;
    oldaddrs[i].sin_port = htons(4242);
  }
  oldaddrs[0].sin_addr.s_addr = inet_addr("127.0.0.11");
  oldaddrs[1].sin_addr.s_addr = inet_addr("127.0.0.21");
  oldaddrs[2].sin_addr.s_addr = inet_addr("127.0.0.31");

  sockaddr_in newaddrs[NUM_NODES];
  for (int i = 0; i < NUM_NODES; i++) {
    newaddrs[i].sin_family = AF_INET;
    newaddrs[i].sin_port = htons(4242);
  }
  newaddrs[0].sin_addr.s_addr = inet_addr("127.0.0.10");
  newaddrs[1].sin_addr.s_addr = inet_addr("127.0.0.20");
  newaddrs[2].sin_addr.s_addr = inet_addr("127.0.0.30");

  std::vector<sockaddr_in> newaddrs_vec;
  std::vector<sockaddr_in> oldaddrs_vec;
  for (int i = 0; i < NUM_NODES; i++) {
    newaddrs_vec.push_back(newaddrs[i]);
    oldaddrs_vec.push_back(oldaddrs[i]);
  }

  Proxy proxy(newaddrs_vec, oldaddrs_vec);
  // // FIXME temporary for testing proxy
  // {
  //   while (true) {
  //     printf("polling\n");
  //     proxy.poll_for_events(true);
  //     printf("get fds\n");
  //     auto send_fds = proxy.get_fds_with_msgs(0);
  //     if (send_fds.size()) {
  //       proxy.print_state();
  //     }
  //     printf("send\n");
  //     for (const auto &x : send_fds) {
  //       proxy.allow_next_msg(x);
  //     }
  //     send_fds = proxy.get_fds_with_msgs(3);
  //     if (send_fds.size()) {
  //       proxy.print_state();
  //     }
  //     printf("send\n");
  //     for (const auto &x : send_fds) {
  //       proxy.allow_next_msg(x);
  //     }
  //   }
  // }

  std::vector<Manager> managers;
  std::set<int> waiting_nodes;
  for (int i = 0; i < NUM_NODES; i++) {
    std::vector<std::string> command;
    command.push_back(std::string(argv[2]));
    command.push_back(std::to_string(i));

    std::string node_addr(inet_ntoa(oldaddrs[i].sin_addr));
    std::string rafted_prefix("/tmp/rafted_tcpmvp_");
    rafted_prefix.append(node_addr);

    managers.push_back(Filter::Manager(command, oldaddrs[i], newaddrs[i],
                                       rafted_prefix, false));
    waiting_nodes.insert(i);
    // // FIXME temporary for testing virtual clock stuff
    // while (managers[i].to_next_event() != EV_EXIT)
    //   ;
    // exit(1);
  }

  unsigned long long cnt = 0;
  unsigned long long it = 0;
  bool has_sent = false;
  while (true) {
    int node_idx;
    if (waiting_nodes.size() > 0) {
      int waiting_idx = rng() % waiting_nodes.size();
      node_idx = *std::next(waiting_nodes.begin(), waiting_idx);
      printf("[ORCH] Progressing %d out of %lu nodes\n", node_idx,
             waiting_nodes.size());
    } else {
      // everything is currently polling or dead
      node_idx = (cnt++) % NUM_NODES;
      printf("[ORCH] Progressing %d\n", node_idx);
    }

    printf("[ORCH] has_sent: %s\n", has_sent ? "true" : "false");
    if (has_sent) {
      proxy.poll_for_events(true);
      has_sent = false;
    }

    {
      auto send_fds = proxy.get_fds_with_msgs(node_idx);
      proxy.print_state();
      printf("[ORCH] Found %lu fds with waiting messages\n", send_fds.size());
      int count = 0;
      for (const auto &x : send_fds) {
        // if (rng() % 2 == 0) {
        if (proxy.allow_next_msg(x))
          count++;
        count++;
        // }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(count * 50));
    }

    auto &manager = managers[node_idx];
    bool to_continue;
    do {
      to_continue = false;
      Filter::Event ev = manager.to_next_event();
      switch (ev) {
      case Filter::EV_NETWORK:
      case Filter::EV_SYNCFS:
        waiting_nodes.insert(node_idx);
        if (rng() % 100 == 0) {
          printf("[ORCH STATE] Toggled node - %d\n", node_idx);
          manager.toggle_node();
          proxy.toggle_node(node_idx);
          has_sent = false;
        } else {
          int res = manager.allow_event(ev);
          if (ev == EV_NETWORK) {
            if (res < 0) {
              printf("[ORCH] Send failed\n");
            } else {
              has_sent = true;
            }
          }
        }
        break;
      case Filter::EV_DEAD: {
        waiting_nodes.erase(node_idx);
        int x = rng();
        if (x % 10 == 0) {
          printf("[ORCH STATE] Toggled node - %d\n", node_idx);
          fprintf(stderr, "Revived node - %d\n", node_idx);
          manager.toggle_node();
          proxy.toggle_node(node_idx);
          to_continue = true;
        }
        break;
      }
      case Filter::EV_POLLING: {
        waiting_nodes.erase(node_idx);
        break;
      }
      case Filter::EV_EXIT: {
        fprintf(stderr, "[ORCH] node %d exited unexpectedly.\n", node_idx);
        exit(1);
      }
      }
    } while (to_continue);
  }

  printf("[ORCH] finished successfully\n");
}