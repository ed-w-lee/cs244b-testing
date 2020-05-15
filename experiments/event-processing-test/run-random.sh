#!/bin/bash

for i in {1..10};
do echo $i && make cleantest && stdbuf -oL -eL ./orch test ~/Documents/dev/raft-rust/target/debug/examples/tcp_mvp > /tmp/trace;
done
