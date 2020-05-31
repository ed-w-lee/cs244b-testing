#include <random>
#include <set>
#include <vector>

#include "decide.h"

RRandDecider::RRandDecider(std::string seed, size_t num_nodes, size_t node_pref,
                           bool death_enabled, size_t death_rate,
                           size_t revive_rate, size_t fsync_rename_rate,
                           size_t msg_delay_rate, size_t primary_percent,
                           size_t secondary_percent)
    : Decider(), num_nodes(num_nodes), node_pref(node_pref),
      death_enabled(death_enabled), death_rate(death_rate),
      revive_rate(revive_rate), fsync_rename_rate(fsync_rename_rate),
      msg_delay_rate(msg_delay_rate), primary_percent(primary_percent),
      secondary_percent(secondary_percent) {

  std::seed_seq seed_seq(seed.begin(), seed.end());
  rng = std::mt19937(seed_seq);

  node_poll_counts = std::vector<int>(3);
}

void RRandDecider::fill_random(void *buf, size_t buf_len) {
  printf("[DECIDER] filling random with len %lu\n", buf_len);
  for (size_t i = 0; i < buf_len; i += 4) {
    *(int *)((char *)buf + i) = rng();
  }
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
  return node_idx;
}

bool RRandDecider::should_send_msg() { return (rng() % msg_delay_rate != 0); }

bool RRandDecider::should_fail_on_send() { return should_die(); }

bool RRandDecider::should_fail_on_connect() { return should_die(); }

bool RRandDecider::should_fail_on_write() { return should_die(); }

bool RRandDecider::should_fail_on_fsync() { return should_die(); }

bool RRandDecider::should_rename_on_fsync() {
  return (rng() % fsync_rename_rate == 0);
}

bool RRandDecider::should_revive() { return (rng() % revive_rate == 0); }

bool RRandDecider::c_should_fail_on_send() { return should_die(); }

bool RRandDecider::c_should_fail_on_connect() { return should_die(); }

// maintain same rng regardless of death enabled or not
bool RRandDecider::should_die() {
  if (death_enabled) {
    return (rng() % death_rate) == 0;
  } else {
    rng();
    return false;
  }
}