# cs244b-testing
(Ideally deterministic) testing framework for distributed systems
- probably only on Linux x86_64

## Testing Raft Implementation
1. Go to `experiments/event-processing-test/`.
2. Build orchestrator with `make`.
3. Deploy 1000 experiments in 200 separate orchestrators using the `visited` heuristic:
```
./deploy/deploy_orch.py --yaml ./deploy/tcp_mvp.yaml -t 1000 -p 200 -s 0 --mode visited
```
4. If there are any bugs, replay the failed seed `{failed_seed}` while outputting stderr using
```
./deploy/deploy_orch.py --yaml ./deploy/tcp_mvp.yaml --mode replay --input-file /tmp/replay_orch_{failed_seed} --enable-stderr 
```
5. Logs for nodes should exist at `/tmp/filter_{addr}` and clients at `/tmp/client_{idx}`. Logs for the orchestrator itself should exist at `/tmp/trace_NONE`.
