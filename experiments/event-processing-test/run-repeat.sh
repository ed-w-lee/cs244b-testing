#!/bin/bash

for i in {1..3};
do 
    echo $i \
    && ./deploy/deploy_orch.py --yaml ./deploy/tcp_mvp.yaml -t 1 -p 1 -t 1 -s 5 --enable-stderr 2> >(tee /tmp/stderr$i) \
    && mv /tmp/replay_orch_5 /tmp/replay_orch_5_$i
done
