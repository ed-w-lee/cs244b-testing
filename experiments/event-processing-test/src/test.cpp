#include <stdio.h>
#include <sys/auxv.h>
#include <sys/time.h>
#include <time.h>

#include <list>

#include "visited.h"

int main(int argc, char *argv[]) {
  Visited vis(4, 10);

  std::list<std::string> traces = {"a", "b", "c"};
  vis.start_txn(traces);
  vis.register_child(1);
  vis.register_child(2);
  vis.register_child(4);

  vis.start_txn(traces);
  vis.register_child(1);
  vis.register_child(2);
  vis.register_child(4);

  vis.start_txn(traces);
  vis.register_child(1);
  vis.register_child(2);
  vis.register_child(3);

  vis.start_txn(traces);
  vis.register_child(1);
  vis.register_child(2);

  vis.end_txn();

  traces = {"a", "d", "b"};
  vis.start_txn(traces);
  vis.register_child(1);
  vis.register_child(2);
  vis.register_child(4);

  vis.start_txn(traces);
  vis.register_child(1);
  vis.register_child(2);
  vis.register_child(4);

  vis.start_txn(traces);
  vis.register_child(1);
  vis.register_child(2);
  vis.register_child(3);

  vis.start_txn(traces);
  vis.register_child(1);
  vis.register_child(2);

  vis.write_paths("test.tmp");

  Visited vis2(4, 10);
  vis2.read_paths("test.tmp");
  traces = {"a", "c", "e"};
  vis2.start_txn(traces);
  vis2.register_child(1);
  vis2.register_child(4);
  vis2.register_child(5);
  vis2.end_txn();

  vis2.write_paths("test.tmp.bk");
}
