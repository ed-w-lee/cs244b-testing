#!/bin/bash

# 1 = port
# 2 = node1
# 3 = node2
# 4 = node3
rm -rf /tmp/rafted_tcpmvp_$2_$1
rm -rf /tmp/rafted_tcpmvp_$3_$1
rm -rf /tmp/rafted_tcpmvp_$4_$1
rm -rf /tmp/raft_test_persist_$2_$3_$4_$1