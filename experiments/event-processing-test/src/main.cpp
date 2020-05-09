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
#include <cstdlib>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "filter.h"
#include "proxy.h"

using namespace Filter;

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <prog> <arg1> ... <argN>\n", argv[0]);
    return 1;
  }

  std::string str_seed = "David Mazieres";
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

  std::vector<Manager> managers;
  std::set<int> waiting_nodes;
  for (int i = 0; i < NUM_NODES; i++) {
    std::vector<std::string> command;
    command.push_back(std::string(argv[1]));
    command.push_back(std::to_string(i));

    std::string node_addr(inet_ntoa(oldaddrs[i].sin_addr));
    std::string rafted_prefix("/tmp/rafted_tcpmvp_");
    rafted_prefix.append(node_addr);

    managers.push_back(Filter::Manager(command, oldaddrs[i], newaddrs[i],
                                       rafted_prefix, false));
    waiting_nodes.insert(i);
    while (managers[i].to_next_event() != EV_EXIT)
      ;
    exit(1);
  }

  std::vector<sockaddr_in> newaddrs_vec;
  std::vector<sockaddr_in> oldaddrs_vec;
  for (int i = 0; i < NUM_NODES; i++) {
    newaddrs_vec.push_back(newaddrs[i]);
    oldaddrs_vec.push_back(oldaddrs[i]);
  }

  std::vector<Proxy> proxies;
  for (int i = 0; i < NUM_NODES; i++) {
    Proxy proxy(oldaddrs[i], newaddrs[i], newaddrs_vec, oldaddrs_vec);
    proxies.push_back(proxy);
  }

  std::ofstream orch_log("/tmp/orch_log.txt", std::ios_base::trunc);

  unsigned long long cnt = 0;
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
    orch_log << node_idx << "/" << waiting_nodes.size() << std::endl;

    for (auto &proxy : proxies) {
      auto &q = proxy.get_msgs();
      std::vector<int> send_fds;
      send_fds.reserve(q.size());
      for (const auto &pair : q) {
        send_fds.push_back(pair.first);
      }
      printf("q_size: %lu send_fds: [", q.size());
      for (const auto &x : send_fds) {
        printf("%d,", x);
      }
      printf("]\n");

      for (const auto &x : send_fds) {
        if (q.at(x).size() > 0) {
          proxy.print_state();
          if (rng() % 2 == 0) {
            proxy.allow_next_msg(x);
          }
        }
      }
    }

    auto &manager = managers[node_idx];
    bool to_continue;
    do {
      to_continue = false;
      switch (manager.to_next_event()) {
      case Filter::EV_SENDTO:
      case Filter::EV_SYNCFS: {
        orch_log << "waiting" << std::endl;
        waiting_nodes.insert(node_idx);
        int x = rng();
        if (x % 100 == 0) {
          orch_log << "kill" << std::endl;
          printf("[ORCH STATE] Toggled node - %d\n", node_idx);
          manager.toggle_node();
          proxies[node_idx].toggle_node();
        }
        break;
      }
      case Filter::EV_DEAD: {
        orch_log << "dead" << std::endl;
        waiting_nodes.erase(node_idx);
        int x = rng();
        if (x % 10 == 0) {
          orch_log << "revive" << std::endl;
          printf("[ORCH STATE] Toggled node - %d\n", node_idx);
          manager.toggle_node();
          proxies[node_idx].toggle_node();
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
}