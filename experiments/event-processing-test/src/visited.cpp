#include "visited.h"

#include <sstream>
#include <stdexcept>

void Visited::start_txn(std::list<std::string> &traces) {
  end_txn();
  if ((int)traces.size() != chain_length - 1) {
    fprintf(stderr, "[VISITED] Incorrect traces length\n");
    exit(1);
  }
  for (const std::string &token : traces) {
    int childId = add_token(token);
    if (childId > 0) {
      register_child(childId);
    } else {
      fprintf(stderr, "[VISITED] unexpected value for child id for %s\n",
              token.c_str());
      exit(1);
    }
  }
}

// takes child of current node to continue on txn
// increments the child's count by 1
//
// (in txn, called immediately after get_counts)
// moves to given child node
void Visited::register_child(int child) {
  currentNode = currentNode->addElement(child);
}

// returns map from syscall -> count
//
// (in txn, called after register_node or register_syscall)
std::unordered_map<int, size_t> *Visited::get_counts() {
  return currentNode->get_counts();
}

// (in txn, ends the txn)
void Visited::end_txn() { currentNode = rootNode; }

// (either file descriptor or string, whatever you prefer)
// writes the currently explored paths to some file (+ counts, ideally)
// can be in any format, we can do post-processing to extract usable data out
// of it
void Visited::write_paths(std::string out_file) {
  try {
    std::ofstream out_file_stream;
    out_file_stream.open(out_file);
    if (out_file_stream.is_open()) {
      out_file_stream << chain_length << '\n';
      out_file_stream << input_tokens_map.size() << "\n";
      for (std::pair<std::string, int> v : input_tokens_map) {
        out_file_stream << v.first << FILE_VALS_DELIMETER << v.second << "\n";
      }
      out_file_stream << '\n';

      std::list<std::pair<std::unordered_map<int, MapTreeNode *>::iterator,
                          std::unordered_map<int, MapTreeNode *>::iterator>>
          history;
      history.push_back(rootNode->get_iterators());
      bool isRollback = false;
      while (!history.empty()) {
        // std::cout << "DEBUG: " << history.size() << "\n";
        if (isRollback) {
          history.back().first++;
        }
        if (history.back().first == history.back().second) {
          history.pop_back();
          out_file_stream << FILE_ROUTE_DELIMETER << '\n';
          isRollback = true;
        } else {
          isRollback = false;
          printf("DEBUG: %lu %d\n", history.size(),
                 (history.back().first)->first);
          out_file_stream << (history.back().first)->first
                          << FILE_VALS_DELIMETER
                          << (history.back().first)->second->get_count()
                          << FILE_VALS_DELIMETER
                          << (history.back().first)->second->get_old_count()
                          << '\n';
          history.push_back((history.back().first)->second->get_iterators());
        }
      }

      out_file_stream.flush();
      out_file_stream.close();
    } else {
      std::cerr << "[VISITED] ERROR: cannot write to file: " << out_file
                << "\n";
    }
  } catch (std::exception &e) {
    std::cerr << "[VISITED] CRITICAL: exception while write from file: "
              << out_file << e.what() << "\n";
  }
}

void Visited::read_paths(std::string in_file) {
  try {
    std::ifstream input_file_stream(in_file);
    if (input_file_stream.is_open()) {
      std::string one_line;
      std::getline(input_file_stream, one_line);
      int readChainLength = std::stoi(one_line);
      if (chain_length != readChainLength) {
        throw std::invalid_argument("chain lengths differ");
      }

      std::getline(input_file_stream, one_line);
      int mapSize = std::stoi(one_line);
      input_tokens_map.clear();
      for (int i = 0; i < mapSize; i++) {
        if (std::getline(input_file_stream, one_line)) {
          std::string key =
              one_line.substr(0, one_line.find(FILE_VALS_DELIMETER));
          std::string value = one_line.substr(
              one_line.find(FILE_VALS_DELIMETER) + 1, one_line.length());
          int input_token_val = std::stoi(value);
          input_tokens_map[key] = input_token_val;
          if (input_token_val > last_token_id) {
            last_token_id = input_token_val;
          }
          // std::cout << "DEBUG: " << key << ":" << value << "," <<
          // last_token_id
          //           << "\n";
        } else {
          std::cerr << "[VISITED] ERROR: reading map values from file: "
                    << in_file << "\n";
          return;
        }
      }
      last_token_id++;
      // now reading tree
      printf("[VISITED] reading tree\n");
      std::list<MapTreeNode *> curr_depth;
      while (std::getline(input_file_stream, one_line)) {
        if (one_line.size() == 0) {
          curr_depth.push_back(rootNode);
        } else if (one_line.size() == 1 && one_line.compare("#") == 0) {
          // if '#', this must terminate the current depth
          curr_depth.pop_back();
          currentNode = curr_depth.back();
        } else {
          // otherwise, must be entry to put in
          std::istringstream ss(one_line);
          std::string node, old_count, curr_count;
          std::getline(ss, node, FILE_VALS_DELIMETER);
          std::getline(ss, curr_count, FILE_VALS_DELIMETER);
          std::getline(ss, old_count, FILE_VALS_DELIMETER);

          register_child_read(std::stoi(node), (size_t)std::stoi(curr_count));
          curr_depth.push_back(currentNode);
        }
        // std::cout << "DEBUG: " << one_line << "\n";
      }
      input_file_stream.close();
    } else {
      std::cerr << "[VISITED] ERROR: cannot read from file: " << in_file
                << "\n";
      return;
    }
  } catch (std::exception &e) {
    std::cerr << "[VISITED] CRITICAL: exception while read from file: "
              << in_file << " " << e.what() << "\n";
  }
}

int Visited::add_token(const std::string &token) {
  if (input_tokens_map.find(token) == input_tokens_map.end()) {
    input_tokens_map[token] = last_token_id++;
  }

  return input_tokens_map[token];
}

void Visited::register_child_read(int child, size_t to_inc) {
  currentNode = currentNode->addElement(child, true, to_inc);
}