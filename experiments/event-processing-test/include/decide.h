#pragma once

#include <fstream>
#include <random>
#include <set>
#include <vector>

// enum DecideEvent {
//   NEXT_NODE,
//   MSG_SEND,
//   NODE_GETRANDOM,
//   NODE_SEND,
//   NODE_CONNECT,
//   NODE_FSYNC,
//   NODE_WRITE,
// };

enum DecideEvent {
  RANDOM,
  NEXT_NODE,
  SEND_MSG,
  SEND,
  CONNECT,
  WRITE,
  FSYNC_FAIL,
  FSYNC_RENAME,
  REVIVE,
  C_SEND,
  C_CONNECT,
};

class Decider {
public:
  Decider() {}
  ~Decider() {}

  virtual void fill_random(void *buf, size_t buf_len) = 0;

  // assume that whenever this gets called, we know all subsequent methods
  // called will refer to the returned node
  virtual int get_next_node(int num_alive_nodes, std::set<int> &nodes,
                            std::set<int> &clients) = 0;
  virtual bool should_send_msg() = 0;

  virtual bool should_fail_on_send() = 0;
  virtual bool should_fail_on_connect() = 0;

  // don't choose # of bytes to write on failure
  // assume that failures always cause 1/2 to be written
  virtual bool should_fail_on_write() = 0;

  virtual bool should_fail_on_fsync() = 0;
  // don't choose how many renames happen
  // assume that true => all renames, false => no renames
  virtual bool should_rename_on_fsync() = 0;

  virtual bool should_revive() = 0;

  virtual bool c_should_fail_on_send() = 0;
  virtual bool c_should_fail_on_connect() = 0;
};

class RRandDecider : public Decider {
public:
  RRandDecider(std::string seed, std::string trace_file, size_t num_nodes = 3,
               size_t node_pref = 2, bool death_enabled = true,
               size_t death_rate = 400, size_t revive_rate = 30,
               size_t fsync_rename_rate = 5, size_t msg_delay_rate = 5,
               size_t primary_percent = 94, size_t secondary_percent = 3);

  void fill_random(void *buf, size_t buf_len) override;

  int get_next_node(int num_alive_nodes, std::set<int> &nodes,
                    std::set<int> &clients) override;
  bool should_send_msg() override;

  bool should_fail_on_send() override;
  bool should_fail_on_connect() override;

  bool should_fail_on_write() override;
  bool should_fail_on_fsync() override;
  bool should_rename_on_fsync() override;

  bool should_revive() override;

  bool c_should_fail_on_send() override;
  bool c_should_fail_on_connect() override;

private:
  std::ofstream fout;

  std::mt19937 rng;

  size_t num_nodes;
  size_t node_pref;

  bool death_enabled;
  size_t death_rate;
  size_t revive_rate;

  size_t fsync_rename_rate;
  size_t msg_delay_rate;

  size_t primary_percent;
  size_t secondary_percent;

  std::vector<int> node_poll_counts;

  bool should_die();
};

class ReplayDecider : public Decider {
public:
  ReplayDecider(std::string trace_file, size_t num_nodes = 3);

  void fill_random(void *buf, size_t buf_len) override;

  int get_next_node(int num_alive_nodes, std::set<int> &nodes,
                    std::set<int> &clients) override;
  bool should_send_msg() override;

  bool should_fail_on_send() override;
  bool should_fail_on_connect() override;

  bool should_fail_on_write() override;
  bool should_fail_on_fsync() override;
  bool should_rename_on_fsync() override;

  bool should_revive() override;

  bool c_should_fail_on_send() override;
  bool c_should_fail_on_connect() override;

private:
  size_t num_nodes;
  std::ifstream fin;

  bool validate_and_replay(DecideEvent ev);
};