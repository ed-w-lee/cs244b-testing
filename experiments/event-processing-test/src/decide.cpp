#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "decide.h"

static const std::unordered_map<DecideEvent, char> trace_names{
    {RANDOM, 'r'},  {NEXT_NODE, 'n'}, {SEND_MSG, 'm'},   {SEND, 's'},
    {CONNECT, 'c'}, {WRITE, 'w'},     {FSYNC_FAIL, 'f'}, {FSYNC_RENAME, 'a'},
    {REVIVE, 'v'},  {C_SEND, 'p'},    {C_CONNECT, 'q'}};

RRandDecider::RRandDecider(std::string seed, std::string trace_file,
                           size_t num_nodes, size_t node_pref,
                           bool death_enabled, size_t death_rate,
                           size_t revive_rate, size_t fsync_rename_rate,
                           size_t msg_delay_rate, size_t primary_percent)
    : Decider(), num_nodes(num_nodes), node_pref(node_pref),
      death_enabled(death_enabled), death_rate(death_rate),
      revive_rate(revive_rate), fsync_rename_rate(fsync_rename_rate),
      msg_delay_rate(msg_delay_rate), primary_percent(primary_percent) {

  fout = std::ofstream(trace_file, std::ofstream::out | std::ofstream::trunc);

  std::seed_seq seed_seq(seed.begin(), seed.end());
  rng = std::mt19937(seed_seq);

  node_poll_counts = std::vector<int>(3);
}

void RRandDecider::fill_random(void *buf, size_t buf_len) {
  printf("[RANDOM] filling random with len %lu\n", buf_len);
  for (size_t i = 0; i < buf_len; i += 4) {
    int res = rng();
    *(int *)((char *)buf + i) = res;
    fout << trace_names.at(RANDOM) << res << ',';
  }
  fout << std::endl;
}

int RRandDecider::get_next_node(int num_alive_nodes, std::set<int> &nodes,
                                std::set<int> &clients) {
  printf("[RANDOM] getting next node\n");
  size_t tot_avail_nodes = node_pref * nodes.size() + clients.size();
  int node_idx = -1;
  if (tot_avail_nodes > 0 && num_alive_nodes > 0) {
    printf("[RANDOM] choosing from %lu nodes and %lu clients\n", nodes.size(),
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
      } else if ((prop - primary_percent) < ((100 - primary_percent) / 2)) {
        node_idx = 1;
      } else {
        node_idx = 2;
      }
    } else {
      node_idx = min_idx;
    }
    node_poll_counts[node_idx]++;
  }
  printf("[RANDOM] chose %d as node to return\n", node_idx);
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

VisitedDecider::VisitedDecider(std::string seed, std::string trace_file,
                               std::string visited_file, size_t num_nodes,
                               size_t node_pref, size_t num_ops,
                               size_t death_rate, size_t revive_rate,
                               size_t fsync_rename_rate, size_t msg_delay_rate,
                               size_t primary_percent)
    : Decider(), num_nodes(num_nodes), num_ops(num_ops),
      visited_file(visited_file), vis(num_ops, 40), node_pref(node_pref),
      death_rate(death_rate), revive_rate(revive_rate),
      fsync_rename_rate(fsync_rename_rate), msg_delay_rate(msg_delay_rate),
      primary_percent(primary_percent), count(0), past_traces(), curr_node(-1),
      curr_trace(""), fail_factor(1.0) {
  fout = std::ofstream(trace_file, std::ofstream::out | std::ofstream::trunc);

  std::seed_seq seed_seq(seed.begin(), seed.end());
  rng = std::mt19937(seed_seq);

  vis.read_paths(visited_file);

  node_poll_counts = std::vector<int>(num_nodes);
}

void VisitedDecider::fill_random(void *buf, size_t buf_len) {
  // we count fill_random as a purely random event
  printf("[VIS_DEC] filling random with len %lu\n", buf_len);
  for (size_t i = 0; i < buf_len; i += 4) {
    int res = rng();
    *(int *)((char *)buf + i) = res;
    fout << trace_names.at(RANDOM) << res << ',';
  }
  fout << std::endl;
}

int VisitedDecider::get_next_node(int num_alive_nodes, std::set<int> &nodes,
                                  std::set<int> &clients) {
  printf("[VIS_DEC] getting next node\n");

  if (curr_node >= 0) {
    // there was a previous node, update vis with its trace
    std::ostringstream oss;
    oss << curr_node << '-' << curr_trace.str();
    curr_trace.str("");
    curr_trace.clear();
    std::string trace = oss.str();
    past_traces.push_back(trace);
    if (past_traces.size() > num_ops) {
      past_traces.pop_front();
    }
  }

  // use RRandom logic if not yet switched to Visited yet, or choosing nodes
  int node_idx = -1;
  if (count < VISIT_THRESH ||
      ((nodes.size() + clients.size() == 0) || (num_alive_nodes == 0))) {
    count++;
    // just do as random
    size_t tot_avail_nodes = node_pref * nodes.size() + clients.size();
    if (tot_avail_nodes > 0 && num_alive_nodes > 0) {
      printf("[VIS_DEC] choosing from %lu nodes and %lu clients\n",
             nodes.size(), clients.size());
      size_t to_run = rng() % tot_avail_nodes;
      if (to_run < node_pref * nodes.size()) {
        to_run %= nodes.size();
        node_idx = *std::next(nodes.begin(), to_run);
      } else {
        to_run -= node_pref * nodes.size();
        node_idx = *std::next(clients.begin(), to_run);
      }
    } else {
      printf("[VIS_DEC] everything polling or dead\n");
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
        } else if ((prop - primary_percent) < ((100 - primary_percent) / 2)) {
          node_idx = 1;
        } else {
          node_idx = 2;
        }
      } else {
        node_idx = min_idx;
      }
      node_poll_counts[node_idx]++;
    }
  } else {
    // if mixed and in Visited regime, choose randomly from available based on
    // counts
    printf("[VIS_DEC] Mixed and should use visited logic\n");
    // TODO - more advanced logic for order-reduction
    std::list<std::string> my_ops(past_traces);
    while (my_ops.size() < num_ops) {
      my_ops.push_front("NONE");
    }
    vis.start_txn(my_ops);

    std::unordered_map<int, size_t> my_counts;

    for (auto &node : nodes) {
      my_counts[node] = 1;
    }
    for (auto &client : clients) {
      my_counts[client] = 1;
    }

    size_t max = 1;
    auto counts = vis.get_counts();
    for (auto &tup : *counts) {
      if (my_counts.find(tup.first) != my_counts.end()) {
        my_counts[tup.first] += tup.second;
        if (my_counts[tup.first] > max) {
          max = my_counts[tup.first];
        }
      }
    }
    delete (counts);

    size_t total = 0;
    std::unordered_map<int, size_t> probs;
    printf("[VIS_DEC] probs: [ ");
    for (auto &tup : my_counts) {
      probs[tup.first] = (max - tup.second + 1);
      printf("(%d, %lu) ", tup.first, probs[tup.first]);
      total += probs[tup.first];
    }
    printf("]\n");

    // choose based on weights
    uint32_t my_prob = rng() % total;

    uint32_t curr_prob = 0;
    for (auto &prob : probs) {
      curr_prob += prob.second;
      if (curr_prob >= my_prob) {
        node_idx = prob.first;
        break;
      }
    }

    if (node_idx < 0) {
      fprintf(stderr, "[VIS_DEC] unable to get ret for get_next_node\n");
      exit(1);
    }
  }
  printf("[VIS_DEC] chose %d as node to return\n", node_idx);
  fout << trace_names.at(NEXT_NODE) << node_idx << std::endl;
  vis.register_child(node_idx);
  curr_node = node_idx;
  return node_idx;
}

bool VisitedDecider::should_send_msg() {
  // TODO just do same as RRandom for now, since no order-reduction
  vis.register_child(SEND_MSG);
  bool to_ret = ((rng() % msg_delay_rate) != 0);
  fout << trace_names.at(SEND_MSG) << (to_ret ? "1" : "0") << std::endl;
  vis.register_child(to_ret ? SUCCESS : FAILURE);
  return to_ret;
}

bool VisitedDecider::should_rename_on_fsync() {
  vis.register_child(FSYNC_RENAME);
  bool to_ret = ((rng() % fsync_rename_rate) == 0);
  fout << trace_names.at(FSYNC_RENAME) << (to_ret ? "1" : "0") << std::endl;
  vis.register_child(to_ret ? SUCCESS : FAILURE);
  return to_ret;
}

bool VisitedDecider::should_revive() {
  // increased pref for reviving if low # of entries, otherwise RRandom
  vis.register_child(REVIVE);
  bool ret;
  auto counts = vis.get_counts();
  if (counts->find(SUCCESS) != counts->end()) {
    ret = (rng() % revive_rate == 0);
  } else {
    ret = (rng() % ((size_t)(revive_rate / 2)) == 0);
  }
  delete (counts);
  fout << trace_names.at(REVIVE) << (ret ? "1" : "0") << std::endl;
  vis.register_child(ret ? SUCCESS : FAILURE);
  return ret;
}

bool VisitedDecider::should_fail_on_send() { return should_die(SEND); }
bool VisitedDecider::should_fail_on_connect() { return should_die(CONNECT); }
bool VisitedDecider::should_fail_on_write() { return should_die(WRITE); }
bool VisitedDecider::should_fail_on_fsync() { return should_die(FSYNC_FAIL); }

bool VisitedDecider::c_should_fail_on_send() { return should_die(C_SEND); }
bool VisitedDecider::c_should_fail_on_connect() {
  return should_die(C_CONNECT);
}

bool VisitedDecider::should_die(DecideEvent ev) {
  bool ret;
  vis.register_child(ev);
  if (count < VISIT_THRESH) {
    count++;
    ret = (rng() % death_rate) == 0;
  } else {
    auto counts = vis.get_counts();
    size_t num_succ =
        counts->find(SUCCESS) != counts->end() ? counts->at(SUCCESS) : 0;
    size_t num_fail =
        counts->find(FAILURE) != counts->end() ? counts->at(FAILURE) : 0;
    delete (counts);
    if ((size_t)(num_fail * death_rate * 1.05) < (num_succ + num_fail)) {
      // abnormally low number of failures along this path, let's increase death
      // rate
      fail_factor *= 1.05;
    }
    size_t adjusted_death_rate = (size_t)(death_rate / fail_factor);
    if (adjusted_death_rate == 0) {
      adjusted_death_rate = 1;
    }
    ret = rng() % (adjusted_death_rate) == 0;
  }
  vis.register_child(ret ? FAILURE : SUCCESS);
  fout << trace_names.at(ev) << (ret ? "1" : "0") << std::endl;
  if (ret) {
    // failed, we should clear the trace and only have fail
    curr_trace.str("");
    curr_trace.clear();
    curr_trace << FAILURE;
    fail_factor = 1.0;
  } else {
    curr_trace << ev << ",";
  }
  return ret;
}

void VisitedDecider::write_metadata() { vis.write_paths(visited_file); }