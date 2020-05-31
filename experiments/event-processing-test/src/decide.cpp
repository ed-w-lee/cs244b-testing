#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <unordered_map>

#include "decide.h"

static const std::unordered_map<DecideEvent, char> trace_names{
    {RANDOM, 'r'},  {NEXT_NODE, 'n'}, {SEND_MSG, 'm'},   {SEND, 's'},
    {CONNECT, 'c'}, {WRITE, 'w'},     {FSYNC_FAIL, 'f'}, {FSYNC_RENAME, 'a'},
    {REVIVE, 'v'},  {C_SEND, 'p'},    {C_CONNECT, 'q'}};

RRandDecider::RRandDecider(std::string seed, std::string trace_file,
                           size_t num_nodes, size_t node_pref,
                           bool death_enabled, size_t death_rate,
                           size_t revive_rate, size_t fsync_rename_rate,
                           size_t msg_delay_rate, size_t primary_percent,
                           size_t secondary_percent)
    : Decider(), num_nodes(num_nodes), node_pref(node_pref),
      death_enabled(death_enabled), death_rate(death_rate),
      revive_rate(revive_rate), fsync_rename_rate(fsync_rename_rate),
      msg_delay_rate(msg_delay_rate), primary_percent(primary_percent),
      secondary_percent(secondary_percent) {

  fout = std::ofstream(trace_file, std::ofstream::out | std::ofstream::trunc);

  std::seed_seq seed_seq(seed.begin(), seed.end());
  rng = std::mt19937(seed_seq);

  node_poll_counts = std::vector<int>(3);
}

void RRandDecider::fill_random(void *buf, size_t buf_len) {
  printf("[DECIDER] filling random with len %lu\n", buf_len);
  for (size_t i = 0; i < buf_len; i += 4) {
    int res = rng();
    *(int *)((char *)buf + i) = res;
    fout << trace_names.at(RANDOM) << res << ',';
  }
  fout << std::endl;
}

int RRandDecider::get_next_node(int num_alive_nodes, std::set<int> &nodes,
                                std::set<int> &clients) {
  printf("[DECIDER] getting next node\n");
  size_t tot_avail_nodes = node_pref * nodes.size() + clients.size();
  int node_idx = -1;
  if (tot_avail_nodes > 0 && num_alive_nodes > 0) {
    printf("[DECIDER] choosing from %lu nodes and %lu clients\n", nodes.size(),
           clients.size());
    size_t to_run = rng() % tot_avail_nodes;
    if (to_run < node_pref * nodes.size()) {
      to_run %= nodes.size();
      node_idx = *std::next(nodes.begin(), to_run);
    } else {
      to_run -= node_pref * nodes.size();
      node_idx = *std::next(clients.begin(), to_run);
    }
  } else {
    // everything is currently polling or dead
    int min_cnt = INT32_MAX;
    int min_idx = -1;
    size_t num_mins = 0;
    // TODO could extract this out to reduce the number of times it's called
    for (size_t i = 0; i < num_nodes; i++) {
      if (node_poll_counts[i] < min_cnt) {
        min_idx = i;
        min_cnt = node_poll_counts[i];
        num_mins = 0;
      }

      if (node_poll_counts[i] == min_cnt) {
        num_mins++;
      }
    }
    size_t prop = rng() % 100;
    if (num_mins == num_nodes) {
      if (prop < primary_percent) {
        node_idx = 0;
      } else if (prop < secondary_percent) {
        node_idx = 1;
      } else {
        node_idx = 2;
      }
    } else {
      node_idx = min_idx;
    }
    node_poll_counts[node_idx]++;
  }
  printf("[DECIDER] chose %d as node to return\n", node_idx);
  fout << trace_names.at(NEXT_NODE) << node_idx << std::endl;
  return node_idx;
}

bool RRandDecider::should_send_msg() {
  bool ret = ((rng() % msg_delay_rate) != 0);
  fout << trace_names.at(SEND_MSG) << (ret ? "1" : "0") << std::endl;
  return ret;
}

bool RRandDecider::should_fail_on_send() {
  bool ret = should_die();
  fout << trace_names.at(SEND) << (ret ? "1" : "0") << std::endl;
  return ret;
}

bool RRandDecider::should_fail_on_connect() {
  bool ret = should_die();
  fout << trace_names.at(CONNECT) << (ret ? "1" : "0") << std::endl;
  return ret;
}

bool RRandDecider::should_fail_on_write() {
  bool ret = should_die();
  fout << trace_names.at(WRITE) << (ret ? "1" : "0") << std::endl;
  return ret;
}

bool RRandDecider::should_fail_on_fsync() {
  bool ret = should_die();
  fout << trace_names.at(FSYNC_FAIL) << (ret ? "1" : "0") << std::endl;
  return ret;
}

bool RRandDecider::should_rename_on_fsync() {
  bool ret = rng() % fsync_rename_rate == 0;
  fout << trace_names.at(FSYNC_RENAME) << (ret ? "1" : "0") << std::endl;
  return ret;
}

bool RRandDecider::should_revive() {
  bool ret = (rng() % revive_rate == 0);
  fout << trace_names.at(REVIVE) << (ret ? "1" : "0") << std::endl;
  return ret;
}

bool RRandDecider::c_should_fail_on_send() {
  bool ret = should_die();
  fout << trace_names.at(C_SEND) << (ret ? "1" : "0") << std::endl;
  return ret;
}

bool RRandDecider::c_should_fail_on_connect() {
  bool ret = should_die();
  fout << trace_names.at(C_CONNECT) << (ret ? "1" : "0") << std::endl;
  return ret;
}

// maintain same rng regardless of death enabled or not
bool RRandDecider::should_die() {
  if (death_enabled) {
    return (rng() % death_rate) == 0;
  } else {
    rng();
    return false;
  }
}

ReplayDecider::ReplayDecider(std::string trace_file, size_t num_nodes)
    : Decider(), num_nodes(num_nodes) {
  fin = std::ifstream(trace_file);
}

void ReplayDecider::fill_random(void *buf, size_t buf_len) {
  printf("[REPLAY] Getting next node\n");
  char name = trace_names.at(RANDOM);
  size_t i;
  for (i = 0; i < buf_len; i += 4) {
    char f_name;
    int val;
    char comma;
    fin >> f_name >> val >> comma;
    printf("[REPLAY] f_name: {%c}, val: {%d}, comma: {%c}", f_name, val, comma);
    if (name != f_name) {
      fprintf(stderr,
              "[REPLAY] found differing command (exp: %c | found: %c), "
              "can't replay -- may be non-determinisic or program changed\n",
              name, f_name);
      exit(1);
    }

    *(int *)((char *)buf + i) = val;
  }
}

int ReplayDecider::get_next_node(int num_alive_nodes, std::set<int> &nodes,
                                 std::set<int> &clients) {
  printf("[REPLAY] Getting next node\n");
  char name = trace_names.at(NEXT_NODE);
  char f_name;
  unsigned int decision;
  fin >> f_name >> decision;
  printf("[REPLAY] f_name: %c, decision: %d\n", f_name, decision);
  if (name != f_name) {
    fprintf(stderr,
            "[REPLAY] found differing command (exp: %c | found: %c), "
            "can't replay -- may be non-determinisic or program changed\n",
            name, f_name);
    exit(1);
  }

  size_t tot_alive_nodes = nodes.size() + clients.size();
  if (num_alive_nodes > 0 && tot_alive_nodes > 0) {
    // can choose from clients and nodes
    if (nodes.find(decision) == nodes.end() &&
        clients.find(decision) == clients.end()) {
      fprintf(stderr,
              "[REPLAY] found impossible next node, can't replay -- may be "
              "non-determinisic or program changed\n");
      exit(1);
    }
  } else {
    // can only choose from nodes
    if (decision >= num_nodes) {
      fprintf(stderr,
              "[REPLAY] found impossible next node, can't replay -- may be "
              "non-determinisic or program changed\n");
      exit(1);
    }
  }
  return decision;
}

bool ReplayDecider::should_send_msg() { return validate_and_replay(SEND_MSG); }

bool ReplayDecider::should_fail_on_send() { return validate_and_replay(SEND); }

bool ReplayDecider::should_fail_on_connect() {
  return validate_and_replay(CONNECT);
}

bool ReplayDecider::should_fail_on_write() {
  return validate_and_replay(WRITE);
}

bool ReplayDecider::should_fail_on_fsync() {
  return validate_and_replay(FSYNC_FAIL);
}

bool ReplayDecider::should_rename_on_fsync() {
  return validate_and_replay(FSYNC_RENAME);
}

bool ReplayDecider::should_revive() { return validate_and_replay(REVIVE); }

bool ReplayDecider::c_should_fail_on_send() {
  return validate_and_replay(C_SEND);
}

bool ReplayDecider::c_should_fail_on_connect() {
  return validate_and_replay(C_CONNECT);
}

bool ReplayDecider::validate_and_replay(DecideEvent ev) {
  char name = trace_names.at(ev);
  char f_name;
  int decision;
  fin >> f_name >> decision;
  if (name != f_name) {
    fprintf(stderr,
            "[REPLAY] found differing command (exp: %c | found: %c), "
            "can't replay -- may be non-determinisic or program changed\n",
            name, f_name);
    exit(1);
  }
  return (decision == 1);
}