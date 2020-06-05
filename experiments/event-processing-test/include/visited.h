#pragma once

#include <exception>
#include <fstream>
#include <iostream>
#include <list>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "MapTreeNode.h"

/*
class NodeTrace {
public:
  NodeTrace(int node_idx);

  void add_syscall(short syscall);

  // or to_hash if we can identify a good hash fn
  // if too much memory being consumed, ignore list of syscalls, maybe just
  // record fail/no fail + who sent messages to the node
  std::string to_string();

private:
  // It should be fine to assume there are btwn 6-16 nodes, and 16
  // different syscalls tracked, so we could maybe change types to chars, or
  // compress the internal representation

  int node_idx;
  std::vector<short> syscalls;
};
*/

class Visited {
public:
  // Creates a Visited that tracks `chain_length - 1` str(NodeTrace) and then
  // a variable-length expanded Node Trace, where the maximum value of a node in
  // the Node Trace is `max_val`
  //
  // so if we have (trace -> trace -> node_idx -> syscall_0 -> ... ->
  // syscall_end), all values v starting with node_idx satisfy 0 <= v < max_val
  //
  // ideally, consumes <50 MB of memory
  Visited(int chain_length, int max_val)
      : chain_length(chain_length), max_val(max_val),
        rootNode(new MapTreeNode(max_val)), currentNode(rootNode){};

  // takes vector of length `chain_length - 1` containing str(NodeTraces)
  //
  // (starts txn)
  // stores the node we are currently on
  void start_txn(std::list<std::string> &traces);

  // takes child of current node to continue on txn
  // increments the child's count by 1
  //
  // (in txn, called immediately after get_counts or itself)
  // moves to given child node
  void register_child(int child);

  // returns map from syscall -> count
  //
  // (in txn, called after register_node or register_syscall)
  std::unordered_map<int, size_t> *get_counts();

  // (in txn, ends the txn)
  void end_txn();

  // (either file descriptor or string, whatever you prefer)
  // writes the currently explored paths to some file (+ counts, ideally)
  // can be in any format, we can do post-processing to extract usable data out
  // output format:
  // N - number of tokens in map of string token
  // N lines of pairs of token;id
  // then all registered routes in token id format separated by new line
  void write_paths(std::string out_file);
  void read_paths(std::string in_file);

private:
  int chain_length, max_val, last_token_id = 1;
  std::unordered_map<std::string, int> input_tokens_map;
  MapTreeNode *rootNode = NULL;
  MapTreeNode *currentNode = NULL;
  const std::string FILE_VALS_DELIMETER = ";";
  const std::string FILE_ROUTE_DELIMETER = "---";

  int add_token(const std::string &token);
};