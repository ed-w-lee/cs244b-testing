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

#include <cstdlib>
#include <random>
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
  for (int i = 0; i < NUM_NODES; i++) {
    std::vector<std::string> command;
    command.push_back(std::string(argv[1]));
    command.push_back(std::to_string(i));

    managers.push_back(
        Filter::Manager(command, oldaddrs[i], newaddrs[i], false));
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

  while (true) {
    for (auto &proxy : proxies) {
      auto &q = proxy.get_msgs();
      for (auto &x : q) {
        if (x.second.size() > 0) {
          proxy.allow_next_msg(x.first);
        }
      }
    }
    for (int i = 0; i < NUM_NODES; i++) {
      auto &manager = managers[i];
      switch (manager.to_next_event()) {
      case Filter::EV_SENDTO:
      case Filter::EV_SYNCFS: {
        int x = rng();
        printf("[ORCH] rng for %d: %d\n", i, x);
        if (x % 10 == 0) {
          printf("[ORCH] Toggled node - %d\n", i);
          manager.toggle_node();
          proxies[i].toggle_node();
        }
        continue;
      }
      case Filter::EV_DEAD: {
        int x = rng();
        printf("[ORCH] rng for %d: %d\n", i, x);
        if (x % 2 == 0) {
          printf("[ORCH] Toggled node - %d\n", i);
          manager.toggle_node();
          proxies[i].toggle_node();
          i--; // get node to start listening before anything else
        }
        continue;
      }
      case Filter::EV_POLLING: {
        // printf("[ORCH] node %d is polling\n", i);
        continue;
      }
      case Filter::EV_EXIT: {
        fprintf(stderr, "[ORCH] node %d exited unexpectedly.\n", i);
        exit(1);
      }
      }
    }
  }
}