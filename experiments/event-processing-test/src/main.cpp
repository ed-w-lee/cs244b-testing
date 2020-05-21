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
#include <unordered_map>
#include <vector>

#include "client.h"
#include "fdmap.h"
#include "filter.h"
#include "proxy.h"

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <seed> <prog> <arg1> ... <argN>\n", argv[0]);
    return 1;
  }

  std::string str_seed(argv[1]);
  std::seed_seq seed(str_seed.begin(), str_seed.end());
  std::mt19937 rng(seed);

  const int NUM_NODES = 3;
  const int NUM_CLIENTS = 3;
  FdMap fdmap(NUM_NODES, NUM_CLIENTS);

  sockaddr_in oldaddrs[NUM_NODES];
  sockaddr_in newaddrs[NUM_NODES];
  {
    for (int i = 0; i < NUM_NODES; i++) {
      oldaddrs[i].sin_family = AF_INET;
      oldaddrs[i].sin_port = htons(4242);

      newaddrs[i].sin_family = AF_INET;
      newaddrs[i].sin_port = htons(4242);
    }
    oldaddrs[0].sin_addr.s_addr = inet_addr("127.0.0.11");
    oldaddrs[1].sin_addr.s_addr = inet_addr("127.0.0.21");
    oldaddrs[2].sin_addr.s_addr = inet_addr("127.0.0.31");

    newaddrs[0].sin_addr.s_addr = inet_addr("127.0.0.10");
    newaddrs[1].sin_addr.s_addr = inet_addr("127.0.0.20");
    newaddrs[2].sin_addr.s_addr = inet_addr("127.0.0.30");
  }

  std::vector<sockaddr_in> newaddrs_vec;
  std::vector<sockaddr_in> oldaddrs_vec;
  for (int i = 0; i < NUM_NODES; i++) {
    newaddrs_vec.push_back(newaddrs[i]);
    oldaddrs_vec.push_back(oldaddrs[i]);
  }

  Proxy proxy(fdmap, newaddrs_vec, oldaddrs_vec, NUM_CLIENTS);
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

  std::vector<Filter::Manager> managers;
  std::set<int> waiting_nodes;
  std::unordered_map<int, int> num_polls;
  for (int i = 0; i < NUM_NODES; i++) {
    std::vector<std::string> command;
    command.push_back(std::string(argv[2]));
    command.push_back(std::to_string(i));

    std::string node_addr(inet_ntoa(oldaddrs[i].sin_addr));
    std::string rafted_prefix("/tmp/rafted_tcpmvp_");
    rafted_prefix.append(node_addr);

    managers.push_back(Filter::Manager(i, command, oldaddrs[i], newaddrs[i],
                                       fdmap, rafted_prefix, false));
    waiting_nodes.insert(i);
    num_polls[i] = 0;
    // // FIXME temporary for testing virtual clock stuff
    // while (managers[i].to_next_event() != EV_EXIT)
    //   ;
    // exit(1);
  }

  std::vector<ClientFilter::ClientManager> clients;
  std::set<int> non_recv_clients;
  for (int i = 0; i < NUM_CLIENTS; i++) {
    std::vector<std::string> command;
    command.push_back(std::string(argv[3]));

    int idx = ClientFilter::CLIENT_OFFS + i;
    clients.push_back(ClientFilter::ClientManager(idx, command, fdmap, false));
    non_recv_clients.insert(idx);
  }

  unsigned long long cnt = 0;
  unsigned long long it = 0;
  bool has_sent = false;
  while (true) {
    printf("[ORCH] has_sent: %s\n", has_sent ? "true" : "false");
    if (has_sent) {
      proxy.poll_for_events(true);
      has_sent = false;
    }

    {
      proxy.print_state();
      // jank way to send client responses
      for (int i = 0; i < NUM_CLIENTS; i++) {
        int idx = i + ClientFilter::CLIENT_OFFS;
        auto client_fds = proxy.get_fds_with_msgs(idx);
        int count = 0;
        for (const auto &x : client_fds) {
          while (proxy.has_more(x)) {
            count++;
            non_recv_clients.insert(idx);
            if (proxy.allow_next_msg(x)) {
              count++;
              break;
            }
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(count * 30));
      }
    }

    int node_idx;
    const int FACTOR = 2;
    size_t tot_avail_nodes =
        FACTOR * waiting_nodes.size() + non_recv_clients.size();
    if (tot_avail_nodes > 0) {
      size_t to_run = rng() % tot_avail_nodes;
      if (to_run < FACTOR * waiting_nodes.size()) {
        to_run %= waiting_nodes.size();
        node_idx = *std::next(waiting_nodes.begin(), to_run);
        printf("[ORCH] Progressing node %d / %lu nodes / %lu clients\n",
               node_idx, waiting_nodes.size(), non_recv_clients.size());
      } else {
        to_run -= FACTOR * waiting_nodes.size();
        node_idx = *std::next(non_recv_clients.begin(), to_run);
        printf("[ORCH] Progressing client %d / %lu nodes / %lu clients\n",
               node_idx, waiting_nodes.size(), non_recv_clients.size());
      }
    } else {
      // everything is currently polling or dead
      int prop = rng() % 100;
      int min_cnt = INT32_MAX;
      int min_idx = -1;
      int num_mins = 0;
      for (int i = 0; i < NUM_NODES; i++) {
        if (num_polls[i] < min_cnt) {
          min_idx = i;
          min_cnt = num_polls[i];
          num_mins = 0;
        }

        if (num_polls[i] == min_cnt) {
          num_mins++;
        }
      }
      printf("[ORCH] num_mins: %d for min_cnt: %d\n", num_mins, min_cnt);
      if (num_mins == NUM_NODES) {
        if (prop < 94) {
          node_idx = 0;
        } else if (prop < 97) {
          node_idx = 1;
        } else {
          node_idx = 2;
        }
      } else {
        node_idx = min_idx;
      }
      num_polls[node_idx]++;
      // node_idx = (cnt++) % NUM_NODES;
      printf("[ORCH] Progressing node %d\n", node_idx);
    }
    if (it++ % 100 == 0) {
      fprintf(stderr, "[ORCH] Current node: %d\n", node_idx);
    }

    if (node_idx < ClientFilter::CLIENT_OFFS) {
      // it's a node
      {
        // send outstanding messages to the node
        auto send_fds = proxy.get_fds_with_msgs(node_idx);
        printf("[ORCH] Found %lu fds with waiting messages\n", send_fds.size());
        int count = 0;
        for (const auto &x : send_fds) {
          if (rng() % 5 > 0) {
            if (proxy.allow_next_msg(x))
              count++;
            count++;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(count * 50));
      }

      auto &manager = managers[node_idx];
      bool to_continue;
      do {
        to_continue = false;
        Filter::Event ev = manager.to_next_event();
        switch (ev) {
        case Filter::EV_CONNECT:
        case Filter::EV_SENDTO:
        case Filter::EV_SYNCFS:
          proxy.set_alive(node_idx);
          waiting_nodes.insert(node_idx);
          if (rng() % 10000 == 0) {
            printf("[ORCH STATE] Toggled node - %d\n", node_idx);
            proxy.toggle_node(node_idx);
            manager.toggle_node();
            has_sent = false;
          } else {
            int res = manager.allow_event(ev);
            if (ev == Filter::EV_CONNECT || ev == Filter::EV_SENDTO) {
              if (res < 0) {
                printf("[ORCH] Send/connect failed\n");
              } else {
                has_sent = true;
              }
            }
          }
          break;
        case Filter::EV_DEAD: {
          waiting_nodes.erase(node_idx);
          int x = rng();
          if (x % 1000 == 0) {
            printf("[ORCH STATE] Toggled node - %d\n", node_idx);
            fprintf(stderr, "Revived node - %d\n", node_idx);
            manager.toggle_node();
            proxy.toggle_node(node_idx);
            to_continue = true;
          }
          break;
        }
        case Filter::EV_POLLING: {
          proxy.set_alive(node_idx);
          waiting_nodes.erase(node_idx);
          break;
        }
        case Filter::EV_EXIT: {
          fprintf(stderr, "[ORCH] node %d exited unexpectedly.\n", node_idx);
          exit(1);
        }
        }
      } while (to_continue);
    } else {
      // it's a client
      auto &client = clients[node_idx - ClientFilter::CLIENT_OFFS];
      ClientFilter::Event ev = client.to_next_event();
      switch (ev) {
      case ClientFilter::EV_CLOSE: {
        has_sent = client.handle_close();
        break;
      }
      case ClientFilter::EV_CONNECT:
      case ClientFilter::EV_SENDTO: {
        if (rng() % 100 == 0) {
          printf("[ORCH STATE] Toggled client - %d\n", node_idx);
          proxy.toggle_node(node_idx);
          client.toggle_client();
          has_sent = false;
        } else {
          int res = client.allow_event(ev);
          if (res < 0) {
            printf("[ORCH] Send/connect failed\n");
          } else {
            has_sent = true;
          }
        }
        break;
      }
      case ClientFilter::EV_DEAD: {
        int x = rng();
        if (x % 10 == 0) {
          printf("[ORCH STATE] Toggled client - %d\n", node_idx);
          fprintf(stderr, "Revived client - %d\n", node_idx);
          proxy.toggle_node(node_idx);
          client.toggle_client();
        }
        break;
      }
      case ClientFilter::EV_RECVING: {
        non_recv_clients.erase(node_idx);
        break;
      }
      case Filter::EV_EXIT: {
        fprintf(stderr, "[ORCH] client %d exited unexpectedly.\n", node_idx);
        exit(1);
      }
      }
    }
  }

  printf("[ORCH] finished successfully\n");
}