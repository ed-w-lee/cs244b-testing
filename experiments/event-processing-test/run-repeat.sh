#!/bin/bash

for i in {1..10};
do echo $i && ./deploy/deploy_orch.py --yaml ./deploy/tcp_mvp.yaml -t 1 -p 1 -t 1 -s 5 --enable-stderr 2>/tmp/stderr$i;
done
