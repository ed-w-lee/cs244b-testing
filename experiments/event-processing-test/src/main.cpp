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
#include <functional>
#include <iterator>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "client.h"
#include "decide.h"
#include "fdmap.h"
#include "filter.h"
#include "proxy.h"

static const int NUM_ITERS = 10000;
static const int PRINT_EVERY = 100;
static const int NUM_NODES = 3;
static const int NUM_CLIENTS = 3;

static int run_validate(std::string seed, std::vector<std::string> command) {
  printf("[FILTER] Running validation command: ");
  for (auto &tok : command) {
    printf("<%s> ", tok.c_str());
  }
  printf("\n");
  pid_t child = fork();
  if (child == 0) {
    std::ostringstream oss;
    oss << "/tmp/validate_";
    oss << seed;
    std::string val_file = oss.str();
    int validate = open(val_file.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
    dup2(validate, STDOUT_FILENO);
    const char **args = new const char *[command.size() + 2];
    for (size_t i = 0; i < command.size(); i++) {
      args[i] = command[i].c_str();
    }
    args[command.size()] = NULL;
    printf("Executing %d with ", getpid());
    for (size_t i = 0; i < command.size(); i++) {
      printf("<%s> ", args[i]);
    }
    printf("\n");
    exit(execv(args[0], (char **)args));
  }
  int status;
  while (true) {
    waitpid(child, &status, 0);
    if (WIFEXITED(status)) {
      return WEXITSTATUS(status);
    }
  }
}

static void kill_children() {
  // not sure how to properly kill children properly
  // just send SIGTERM, and then wait a few ms, then exit
  // likely due to children being under ptrace
  signal(SIGQUIT, SIG_IGN);
  kill(0, SIGQUIT);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

enum orch_mode { UNINIT, RAND, REPLAY, VISITED };

enum config_field {
  SPECIFIER,
  MODE,
  SEED,
  NODE_CMD,
  CLIENT_CMD,
  VAL_CMD,
  OLD_ADDRS,
  NEW_ADDRS,
  NODE_DIR,
  LISTEN_PORT,
  REPLAY_FILE,
  VISITED_FILE,
};

struct orch_config {
  orch_mode mode;
  std::string seed;
  std::vector<std::string> node_cmd;
  std::vector<std::string> client_cmd;
  std::vector<std::string> val_cmd;
  std::vector<std::string> old_addrs;
  std::vector<std::string> new_addrs;
  std::string node_dir;
  in_port_t listen_port;
  std::string replay_file;
  std::string visited_file;
};

bool validate_args(int argc, char **argv, orch_config &config) {
  config_field next_arg = SPECIFIER;
  std::unordered_set<std::string> old_addrs_set;
  for (int i = 1; i < argc; i++) {
    std::string arg(argv[i]);
    switch (next_arg) {
    case SPECIFIER: {
      // expect a specifier
      if (arg.length() < 2 || (arg[0] != '-' || arg[1] != '-')) {
        fprintf(stderr, "expected specifier like --mode instead of %s\n",
                argv[i]);
        return false;
      } else {
        std::string actual_spec = arg.substr(2);
        if (actual_spec.compare("mode") == 0) {
          next_arg = MODE;
        } else if (actual_spec.compare("seed") == 0) {
          next_arg = SEED;
        } else if (actual_spec.compare("node") == 0) {
          next_arg = NODE_CMD;
        } else if (actual_spec.compare("client") == 0) {
          next_arg = CLIENT_CMD;
        } else if (actual_spec.compare("val") == 0) {
          next_arg = VAL_CMD;
        } else if (actual_spec.compare("old-addrs") == 0) {
          next_arg = OLD_ADDRS;
        } else if (actual_spec.compare("new-addrs") == 0) {
          next_arg = NEW_ADDRS;
        } else if (actual_spec.compare("node-dir") == 0) {
          next_arg = NODE_DIR;
        } else if (actual_spec.compare("listen-port") == 0) {
          next_arg = LISTEN_PORT;
        } else if (actual_spec.compare("replay-file") == 0) {
          next_arg = REPLAY_FILE;
        } else if (actual_spec.compare("visited-file") == 0) {
          next_arg = VISITED_FILE;
        } else {
          fprintf(stderr, "unexpected specifier %s\n", actual_spec.c_str());
          return false;
        }
      }
      break;
    }
    case MODE: {
      next_arg = SPECIFIER;
      if (arg.compare("rand") == 0) {
        config.mode = orch_mode::RAND;
      } else if (arg.compare("replay") == 0) {
        config.mode = orch_mode::REPLAY;
      } else if (arg.compare("visited") == 0) {
        config.mode = orch_mode::VISITED;
      } else {
        fprintf(stderr, "unexpected mode %s\n", arg.c_str());
        return false;
      }
      break;
    }
    case SEED: {
      next_arg = SPECIFIER;
      if (arg.length() == 0) {
        fprintf(stderr, "seed should not be empty\n");
        return false;
      } else {
        config.seed = arg;
      }
      break;
    }
    case NODE_CMD:
    case CLIENT_CMD:
    case VAL_CMD: {
      if (arg.length() == 0) {
        fprintf(stderr, "%s should not be empty", argv[i - 1] + 2);
        return false;
      } else {
        std::istringstream iss(arg);
        std::string token;
        std::vector<std::string> cmd;
        while (std::getline(iss, token, '#')) {
          cmd.push_back(token);
        }
        if (next_arg == NODE_CMD) {
          config.node_cmd = cmd;
        } else if (next_arg == CLIENT_CMD) {
          config.client_cmd = cmd;
        } else {
          config.val_cmd = cmd;
        }
      }
      next_arg = SPECIFIER;
      break;
    }
    case OLD_ADDRS:
    case NEW_ADDRS: {
      std::istringstream iss(arg);
      std::vector<std::string> addrs{std::istream_iterator<std::string>{iss},
                                     std::istream_iterator<std::string>{}};
      if (addrs.size() != NUM_NODES) {
        fprintf(stderr, "%s needs %d addrs\n", argv[i - 1] + 2, NUM_NODES);
        return false;
      }
      struct in_addr ad;
      for (const auto &addr : addrs) {
        if (inet_aton(addr.c_str(), &ad) == 0) {
          fprintf(stderr, "couldn't parse addr %s, or it's 255.255.255.255\n",
                  addr.c_str());
          return false;
        }
      }

      if (next_arg == OLD_ADDRS) {
        config.old_addrs = addrs;
        old_addrs_set =
            std::unordered_set<std::string>{addrs.begin(), addrs.end()};
      } else {
        config.new_addrs = addrs;
      }
      next_arg = SPECIFIER;
      break;
    }
    case NODE_DIR: {
      next_arg = SPECIFIER;
      if (arg.length() == 0) {
        fprintf(stderr, "node-dir should not be empty\n");
        return false;
      } else {
        config.node_dir = arg;
      }
      break;
    }
    case LISTEN_PORT: {
      next_arg = SPECIFIER;
      try {
        int potential_port = std::atoi(arg.c_str());
        if (potential_port < std::numeric_limits<short>::max() &&
            potential_port > 0) {
          config.listen_port = potential_port;
        } else {
          fprintf(stderr, "listen-port out of bounds\n");
          return false;
        }
      } catch (...) {
        fprintf(stderr, "failed to parse listen-port %s\n", arg.c_str());
        return false;
      }
      break;
    }
    case REPLAY_FILE: {
      next_arg = SPECIFIER;
      if (arg.length() == 0) {
        fprintf(stderr, "replay-file should not be empty\n");
        return false;
      } else {
        config.replay_file = arg;
      }
      break;
    }
    case VISITED_FILE: {
      next_arg = SPECIFIER;
      if (arg.length() == 0) {
        fprintf(stderr, "visited-file should not be empty\n");
        return false;
      } else {
        config.visited_file = arg;
      }
      break;
    }
    }
  }

  if (config.mode == orch_mode::UNINIT || config.node_cmd.empty() ||
      config.client_cmd.empty() || config.val_cmd.empty() ||
      config.node_dir.empty() || config.listen_port == 0 ||
      config.replay_file.empty()) {
    fprintf(stderr, "missing some arg\n");
    return false;
  }
  if ((config.mode == orch_mode::RAND || config.mode == orch_mode::VISITED) &&
      config.seed.empty()) {
    fprintf(stderr, "rand/visited mode requires seed\n");
    return false;
  }
  if (config.mode == orch_mode::VISITED && config.visited_file.empty()) {
    fprintf(stderr, "visited mode requires visited file\n");
    return false;
  }
  for (const auto &new_addr : config.new_addrs) {
    if (old_addrs_set.find(new_addr) != old_addrs_set.end()) {
      fprintf(stderr, "new addr %s is in old addrs\n", new_addr.c_str());
      return false;
    }
  }

  printf("[ORCH] validated config successfully:\n");
  printf("[ORCH] parsed config:\n");
  printf("       - mode: %s\n",
         config.mode == orch_mode::RAND ? "rand" : "visited");
  printf("       - seed: %s\n", config.seed.c_str());
  printf("       - node_cmd: [ ");
  for (const auto &tok : config.node_cmd) {
    printf("%s ", tok.c_str());
  }
  printf("]\n");
  printf("       - client_cmd: [ ");
  for (const auto &tok : config.client_cmd) {
    printf("%s ", tok.c_str());
  }
  printf("]\n");
  printf("       - val_cmd: [ ");
  for (const auto &tok : config.val_cmd) {
    printf("%s ", tok.c_str());
  }
  printf("]\n");
  printf("       - old_addrs: [ ");
  for (const auto &old_addr : config.old_addrs) {
    printf("%s ", old_addr.c_str());
  }
  printf("]\n");
  printf("       - new_addrs: [ ");
  for (const auto &new_addr : config.new_addrs) {
    printf("%s ", new_addr.c_str());
  }
  printf("]\n");
  printf("       - node_dir: \"%s\"\n", config.node_dir.c_str());
  printf("       - listen_port: %hu\n", config.listen_port);
  printf("       - replay_file: %s\n", config.replay_file.c_str());

  return true;
}

// Returns:
// - 0 if successful
// - 1 if orch failed unexpectedly
// - 2 if node failed
// - 3 if client failed
// - 4 if validation failed
// - 5 if arguments wrong
int main(int argc, char **argv) {
  orch_config config = {
      orch_mode::UNINIT, // mode
      "CS244B",          // seed
      {},                // node_cmd
      {},                // client_cmd
      {},                // val_cmd
      {},                // old_addrs
      {},                // new_addrs
      "",                // node_dir
      0,                 // listen_port
      "",                // replay file
      ""                 // visited file
  };
  if (!validate_args(argc, argv, config)) {
    // too lazy to do proper arg parsing
    fprintf(stderr,
            "Usage: %s\n"
            "--mode (rand|replay|visited) \n"
            "--seed <seed> \n"
            "--node \"<prog> <args>\"\n"
            "\t- allows {addr} for node's addr "
            "and {o_addrs} for all other addrs\n"
            "--client \"<client> <args>\"\n"
            "--val \"<validate> <args>\"\n"
            "--old-addrs \"<addr> <addr> ...\"\n"
            "--new-addrs \"<addr> <addr> ...\"\n"
            "--node-dir \"<dir>\"\n"
            "\t- allows {addr} for node's addr\n"
            "--listen-port <port>\n"
            "--replay-file <file>\n"
            "\t- if mode=replay, replay <file>. otherwise create replayable "
            "trace in <file>\n"
            "--visited-file <file>\n"
            "\t- if mode=visited, read (if exists). for mode=(rand|visited) "
            "write visited paths from <file>"
            "\n"
            "commands should be delimited by #, not spaces\n",
            argv[0]);
    exit(1);
  }

  // needed for the entire lifetime of the program, so just let it die
  Decider *decider;
  switch (config.mode) {
  case orch_mode::RAND: {
    decider =
        new RRandDecider(config.seed, config.replay_file, config.visited_file);
    break;
  }
  case orch_mode::REPLAY: {
    decider = new ReplayDecider(config.replay_file);
    break;
  }
  case orch_mode::VISITED: {
    decider = new VisitedDecider(config.seed, config.replay_file,
                                 config.visited_file);
    break;
  }
  default: {
    fprintf(stderr, "unsupported mode\n");
    exit(1);
  }
  }
  FdMap fdmap(NUM_NODES, NUM_CLIENTS);

  std::vector<sockaddr_in> newaddrs;
  std::vector<sockaddr_in> oldaddrs;
  for (int i = 0; i < NUM_NODES; i++) {
    printf("%s -> %s\n", config.old_addrs[i].c_str(),
           config.new_addrs[i].c_str());
    {
      struct sockaddr_in newaddr;
      // addr already validated, so use inet_addr
      newaddr.sin_addr.s_addr = inet_addr(config.new_addrs[i].c_str());
      newaddr.sin_family = AF_INET;
      newaddr.sin_port = htons(config.listen_port);
      newaddrs.push_back(newaddr);
    }
    {
      struct sockaddr_in oldaddr;
      // addr already validated, so use inet_addr
      oldaddr.sin_addr.s_addr = inet_addr(config.old_addrs[i].c_str());
      oldaddr.sin_family = AF_INET;
      oldaddr.sin_port = htons(config.listen_port);
      oldaddrs.push_back(oldaddr);
    }
  }
  printf("newaddr: %lu, oldaddr: %lu\n", newaddrs.size(), oldaddrs.size());
  for (int i = 0; i < NUM_NODES; i++) {
    printf("%s:%hu -> %s:%hu\n", inet_ntoa(oldaddrs[i].sin_addr),
           ntohs(oldaddrs[i].sin_port), inet_ntoa(newaddrs[i].sin_addr),
           ntohs(newaddrs[i].sin_port));
  }

  Proxy proxy(fdmap, newaddrs, oldaddrs, NUM_CLIENTS);
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
    std::string node_addr = config.old_addrs[i];
    std::vector<std::string> command;
    for (const auto &tok : config.node_cmd) {
      if (tok.compare("{addr}") == 0) {
        command.push_back(node_addr);
      } else if (tok.compare("{o_addrs}") == 0) {
        for (int j = 0; j < NUM_NODES; j++) {
          if (i != j) {
            command.push_back(inet_ntoa(oldaddrs[j].sin_addr));
          }
        }
      } else {
        command.push_back(tok);
      }
    }

    std::string node_dir;
    size_t found = 0;
    size_t next;
    while (true) {
      next = config.node_dir.find("{addr}", found);
      if (next == config.node_dir.npos) {
        break;
      }
      node_dir.append(config.node_dir.substr(found, next - found));
      node_dir.append(node_addr);
      found = next + 6;
    }
    node_dir.append(config.node_dir.substr(found));
    managers.push_back(Filter::Manager(i, command, oldaddrs[i], newaddrs[i],
                                       fdmap, node_dir, false));
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
    int idx = ClientFilter::CLIENT_OFFS + i;
    clients.push_back(ClientFilter::ClientManager(
        idx, config.seed, config.client_cmd, fdmap, false));
    non_recv_clients.insert(idx);
  }

  unsigned long long cnt = 0;
  unsigned long long it = 0;
  int num_alive_nodes = NUM_NODES;
  while (NUM_ITERS <= 0 || it++ < NUM_ITERS) {
    {
      printf("[ORCH] printing state\n");
      proxy.print_state();
      printf("[ORCH] finished printing state\n");
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

    int node_idx = decider->get_next_node(num_alive_nodes, waiting_nodes,
                                          non_recv_clients);

    if ((it % PRINT_EVERY) == 0) {
      fprintf(stderr, "[ORCH] Current node: %d\n", node_idx);
      printf("[ORCH] validating\n");
      for (auto &mgr : managers) {
        mgr.setup_validate();
      }
      bool res = run_validate(config.seed, config.val_cmd);
      for (auto &mgr : managers) {
        mgr.finish_validate();
      }
      if (res) {
        fflush(stdout);
        fprintf(stderr, "[ORCH] Validation failed\n\n");
        printf("[ORCH] Validation failed\n\n");
        kill_children();
        decider->write_metadata();
        exit(4);
      }
    }

    if (node_idx < ClientFilter::CLIENT_OFFS) {
      // it's a node
      {
        // send outstanding messages to the node
        auto send_fds = proxy.get_fds_with_msgs(node_idx);
        printf("[ORCH] Found %lu fds with waiting messages\n", send_fds.size());
        int count = 0;
        for (const auto &x : send_fds) {
          if (decider->should_send_msg()) {
            if (proxy.allow_next_msg(x))
              count++;
            count++;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(count * 50));
      }

      auto &manager = managers[node_idx];
      bool to_continue;
      bool has_sent;
      do {
        has_sent = false;
        to_continue = false;
        Filter::Event ev = manager.to_next_event();
        switch (ev) {
        case Filter::EV_RANDOM: {
          manager.handle_getrandom(ev, [&](void *buf, size_t buflen) -> void {
            decider->fill_random(buf, buflen);
          });
          to_continue = true;
          break;
        }
        case Filter::EV_CONNECT:
        case Filter::EV_SENDTO: {
          proxy.set_alive(node_idx);
          waiting_nodes.insert(node_idx);
          if ((ev == Filter::EV_CONNECT && decider->should_fail_on_connect()) ||
              (ev == Filter::EV_SENDTO && decider->should_fail_on_send())) {
            num_alive_nodes--;
            printf("[ORCH STATE] Toggled node before network - %d, %d left\n",
                   node_idx, num_alive_nodes);
            fprintf(stderr, "Killed node before network - %d\n", node_idx);
            for (auto &tup : proxy.toggle_node(node_idx)) {
              if (tup.first >= ClientFilter::CLIENT_OFFS) {
                // node died while there was a client connection, make sure the
                // client is available to run
                printf("[ORCH] re-enabling client %d\n", tup.first);
                non_recv_clients.insert(tup.first);
              }
            }
            fflush(stdout);
            manager.toggle_node();
          } else {
            int res = manager.allow_event(ev);
            if (res < 0) {
              printf("[ORCH] Send/connect failed\n");
            } else {
              has_sent = true;
            }
            to_continue = true;
          }
          break;
        }
        case Filter::EV_WRITE: {
          proxy.set_alive(node_idx);
          waiting_nodes.insert(node_idx);
          int ret = manager.handle_write(ev, [&](size_t max_write) -> size_t {
            if (decider->should_fail_on_write()) {
              return max_write / 2;
            } else {
              return max_write;
            }
          });
          if (ret < 0) {
            num_alive_nodes--;
            printf("[ORCH STATE] Toggled node during write - %d, %d left\n",
                   node_idx, num_alive_nodes);
            fprintf(stderr, "Killed node during write - %d\n", node_idx);
            for (auto &tup : proxy.toggle_node(node_idx)) {
              if (tup.first >= ClientFilter::CLIENT_OFFS) {
                // node died while there was a client connection, make sure the
                // client is available to run
                printf("[ORCH] re-enabling client %d\n", tup.first);
                non_recv_clients.insert(tup.first);
              }
            }
            fflush(stdout);
            manager.toggle_node();
          } else {
            to_continue = true;
          }
          break;
        }
        case Filter::EV_FSYNC: {
          proxy.set_alive(node_idx);
          waiting_nodes.insert(node_idx);
          if (decider->should_fail_on_fsync()) {
            num_alive_nodes--;
            printf("[ORCH STATE] Toggled node before fsync - %d, %d left\n",
                   node_idx, num_alive_nodes);
            fprintf(stderr, "Killed node before fsync - %d\n", node_idx);
            for (auto &tup : proxy.toggle_node(node_idx)) {
              if (tup.first >= ClientFilter::CLIENT_OFFS) {
                // node died while there was a client connection, make sure the
                // client is available to run
                printf("[ORCH] re-enabling client %d\n", tup.first);
                non_recv_clients.insert(tup.first);
              }
            }
            fflush(stdout);
            manager.toggle_node();
          } else {
            manager.handle_fsync(ev, [&](size_t max_ops) -> size_t {
              if (decider->should_rename_on_fsync()) {
                return max_ops;
              } else {
                return 0;
              }
            });
            to_continue = true;
          }
          break;
        }
        case Filter::EV_DEAD: {
          waiting_nodes.erase(node_idx);
          if (decider->should_revive()) {
            num_alive_nodes++;
            printf("[ORCH STATE] Revived node - %d, %d left\n", node_idx,
                   num_alive_nodes);
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
          printf("[ORCH] node %d exited unexpectedly.\n", node_idx);
          fflush(stdout);
          kill_children();
          decider->write_metadata();
          exit(2);
        }
        }
        if (has_sent) {
          proxy.poll_for_events(true);
          has_sent = false;
        }
      } while (to_continue);
    } else {
      // it's a client
      auto &client = clients[node_idx - ClientFilter::CLIENT_OFFS];

      bool has_sent, to_continue;
      do {
        has_sent = false;
        to_continue = false;

        ClientFilter::Event ev = client.to_next_event();
        switch (ev) {
        case ClientFilter::EV_CLOSE: {
          has_sent = client.handle_close();
          to_continue = true;
          break;
        }
        case ClientFilter::EV_CONNECT:
        case ClientFilter::EV_SENDTO: {
          if ((ev == ClientFilter::EV_CONNECT &&
               decider->c_should_fail_on_connect()) ||
              (ev == ClientFilter::EV_SENDTO &&
               decider->c_should_fail_on_send())) {
            fprintf(stderr, "Killed client - %d\n", node_idx);
            printf("[ORCH STATE] Toggled client - %d\n", node_idx);
            proxy.toggle_node(node_idx);
            client.toggle_client();
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
          // just revive since client being dead doesn't really change anything
          fprintf(stderr, "Revived client - %d\n", node_idx);
          printf("[ORCH STATE] Toggled client - %d\n", node_idx);
          proxy.toggle_node(node_idx);
          client.toggle_client();
          to_continue = true;
          break;
        }
        case ClientFilter::EV_RECVING: {
          non_recv_clients.erase(node_idx);
          break;
        }
        case Filter::EV_EXIT: {
          fflush(stdout);
          fprintf(stderr, "[ORCH] client %d exited unexpectedly.\n", node_idx);
          printf("[ORCH] client %d exited unexpectedly.\n", node_idx);
          kill_children();
          decider->write_metadata();
          exit(3);
        }
        }
        if (has_sent) {
          proxy.poll_for_events(true);
          has_sent = false;
        }
      } while (to_continue);
    }
  }

  printf("[ORCH] finished successfully\n");
  fflush(stdout);
  kill_children();
  decider->write_metadata();
  exit(0);
}