#!/usr/bin/env python3
import argparse
import yaml
import json
import os
import gc
import sys
import time
import errno
import shlex
import subprocess
from collections import deque

from visited import VisitedTree

__location__ = os.path.realpath(
    os.path.join(os.getcwd(), os.path.dirname(__file__)))


# converts list of intervals + singular values into a list of singular values
def enumerate_ranges(range_list):
  to_ret = []
  for rng in range_list:
    if isinstance(rng, list):
      if len(rng) != 2:
        return (False, rng)
      else:
        for a in range(rng[0], rng[1] + 1):
          to_ret.append(a)
    elif isinstance(rng, int):
      to_ret.append(rng)
    else:
      return (False, rng)
  return (True, sorted(list(set(to_ret))))


def validate_config(conf, p):
  print('validating...')
  if not conf:
    return (False, "conf doesn't exist: {}".format(conf))
  fields = [
      'clean', 'node_cmd', 'client_cmd', 'val_cmd', 'addr_range', 'node_dir',
      'listen_ports'
  ]
  for field in fields:
    if field not in conf or not conf[field]:
      return (False, 'missing field {}'.format(field))

  status, addrs = enumerate_ranges(conf['addr_range'])
  if status == False:
    return (False, 'unable to parse range {}'.format(addrs))
  addrs = ['127.0.0.{}'.format(a) for a in addrs]

  status, ports = enumerate_ranges(conf['listen_ports'])
  if status == False:
    return (False, 'unable to parse range {}'.format(ports))

  num_full_addrs = len(addrs) * len(ports)
  if num_full_addrs < 6 * p:
    return (False,
            'too few addrs ({}) for {} parallel clusters, minimum is {}'.format(
                num_full_addrs, p, 6 * p))
  del conf['addr_range']
  conf['addrs'] = addrs
  del conf['listen_ports']
  conf['ports'] = ports
  return (True, conf)


def manage_orch(conf,
                port,
                seed,
                addrs,
                enable_stdout,
                enable_stderr,
                mode='rand',
                input_file='/tmp/replay_orch_{seed}',
                my_vis=None):
  '''
  Manages an orch instance. Runs in a separate process in case we need to
  communicate with the instance.
  Forks off an orch with the given config and seed, and then waits for completion
  '''
  proxy_addrs = addrs[::2]
  node_addrs = addrs[1::2]
  print('attempting to start orch {} with seed {}'.format(os.getpid(), seed))

  format_nodes = {
      'addr0': node_addrs[0],
      'addr1': node_addrs[1],
      'addr2': node_addrs[2],
      'addrs': ' '.join(node_addrs),
      'o_addrs': '{o_addrs}',
      'addr': '{addr}',
      'port': port,
  }

  # start by cleaning up seed just in case
  my_clean_cmd = [
      os.path.join(__location__, 'cleanup.sh'),
      str(seed), *node_addrs,
      str(port)
  ]
  subprocess.run(my_clean_cmd)
  print('finished cleaning with {}'.format(my_clean_cmd))
  clean_cmd = shlex.split(conf['clean'].format(**format_nodes))
  subprocess.run(clean_cmd)
  print('finished cleaning with {}'.format(clean_cmd))

  if mode == 'visited' and my_vis:
    print('writing visited file for {}'.format(seed))
    my_vis.write_to('/tmp/visited_{mode}_{seed}'.format(mode=mode, seed=seed))

  delim = '#'  # just use as delimiter (assume typical commands don't include #)
  conf['my_node_cmd'] = delim.join(
      shlex.split(conf['node_cmd'].format(**format_nodes)))
  conf['my_client_cmd'] = delim.join(
      shlex.split(conf['client_cmd'].format(**format_nodes)))
  conf['my_val_cmd'] = delim.join(
      shlex.split(conf['val_cmd'].format(**format_nodes)))
  input_file = input_file.format(seed=seed)
  command = '''
            ./orch 
            --mode '{mode}'
            --seed '{seed}'
            --node '{my_node_cmd}' 
            --client '{my_client_cmd}' 
            --val '{my_val_cmd}'
            --node-dir '{node_dir}'
            --listen-port '{port}'
            --replay-file '{input_file}'
            --visited-file '/tmp/visited_{mode}_{seed}'
            '''
  command = command.format(mode=mode,
                           seed=seed,
                           port=port,
                           input_file=input_file,
                           **conf)
  command = command.format(**format_nodes)
  command += '''
             --old-addrs '{node_addrs}'
             --new-addrs '{proxy_addrs}'
             '''.format(node_addrs=' '.join(node_addrs),
                        proxy_addrs=' '.join(proxy_addrs))
  command = shlex.split(command)
  trace_file = '/tmp/trace_{}'.format(seed)
  print('attempting to run {}'.format(' '.join(
      [shlex.quote(arg) for arg in command])))
  try:
    child_pid = os.fork()
    if child_pid == 0:
      os.setpgid(0, 0)
      if not enable_stdout:
        out = os.open(trace_file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC)
        os.dup2(out, 1)
      if not enable_stderr:
        devnull = os.open('/dev/null', os.O_WRONLY)
        os.dup2(devnull, 2)
      exit(os.execv(command[0], command))
    return child_pid
  except OSError as e:
    print('manage_orch failed')
    return -1


def wait_for_children_to_finish():
  print('waiting for children to exit...')
  while True:
    try:
      pid, status = os.waitpid(-1, 0)
    except OSError as e:
      if e.errno == errno.ECHILD:
        print("done waiting")
        break
      print("unexpected waitpid error: {}".format(e))
      exit(1)


def deploy_orchs(conf, mode, seed, parallel, total, enable_stdout,
                 enable_stderr):
  print('deploying...')
  num_rounds = 0
  num_completed = 0
  # iterator magic from: https://stackoverflow.com/a/5389547
  addr_sets = list(zip(*[iter(conf['addrs'])] * 6))
  free_addrs = deque([
      (port, addr_set) for addr_set in addr_sets for port in conf['ports']
  ])
  first_seed = seed
  child_status = {}
  for _ in range(min(total, parallel)):
    # take next 6 addrs
    port, child_addrs = free_addrs.popleft()
    child_pid = manage_orch(conf=conf,
                            port=port,
                            addrs=child_addrs,
                            seed=seed,
                            enable_stdout=enable_stdout,
                            enable_stderr=enable_stderr,
                            mode=mode)
    if child_pid == -1:
      exit(1)
    child_status[child_pid] = (port, seed, child_addrs)
    num_rounds += 1
    seed += 1

  my_vis = VisitedTree()
  exit_statuses = [None] * total
  path_counts = []
  while True:
    try:
      pid, status = os.waitpid(-1, 0)
    except OSError as e:
      print("unexpected waitpid error: {}".format(e))
      break

    child_port, child_seed, child_addrs = child_status[pid]
    if not os.WIFEXITED(status):
      print("unexpected status, didn't exit: ({}, {})".format(
          child_seed, status))
      wait_for_children_to_finish()
      exit(1)
    exit_status = os.WEXITSTATUS(status)
    print('manage_orch {} seed {} exited with {}'.format(
        pid, child_seed, exit_status))
    exit_statuses[child_seed - first_seed] = exit_status
    if exit_status >= 100:
      # likely manage_orch exited
      print('manage_orch {}, seed {} exited unexpectedly'.format(
          pid, child_seed, exit_status))
      wait_for_children_to_finish()
      exit(1)
    elif exit_status == 1:
      # orch failed. we'd want to keep running, but for now just exit
      print('orch {} failed'.format(pid))
      wait_for_children_to_finish()
      exit(1)

    del child_status[pid]
    free_addrs.append((child_port, child_addrs))

    # update my_vis

    succ_vis = VisitedTree()
    res = succ_vis.get_from_file('/tmp/visited_{mode}_{seed}'.format(
        mode=mode, seed=child_seed),
                                 relative=True)
    if res:
      succ_vis.validate()
      my_vis.add_other(succ_vis)
      my_vis.validate()
    succ_vis = None
    gc.collect()
    path_counts.append(my_vis.num_paths())

    # clean up everything but trace (unless run failed)
    if total > 1:
      node_addrs = child_addrs[1::2]
      subprocess.run([
          os.path.join(__location__, 'cleanup.sh'),
          str(child_seed), *node_addrs,
          str(child_port)
      ])
      clean_cmd = shlex.split(conf['clean'].format(
          port=child_port, addrs=' '.join(sorted(child_addrs)[1::2])))
      res = subprocess.run(clean_cmd)
    if exit_status == 0 and not enable_stdout:
      # since run succeeded, clean up replay trace as well
      subprocess.run(['rm', '-f', '/tmp/replay_orch_{}'.format(child_seed)])

    time.sleep(1)
    num_completed += 1

    if num_rounds < total:
      num_rounds += 1
      port, child_addrs = free_addrs.popleft()
      child_pid = manage_orch(conf=conf,
                              port=port,
                              addrs=child_addrs,
                              seed=seed,
                              enable_stdout=enable_stdout,
                              enable_stderr=enable_stderr,
                              mode=mode,
                              my_vis=my_vis)
      if child_pid == -1:
        exit(1)
      child_status[child_pid] = (port, seed, child_addrs)
      seed += 1
    elif num_completed >= total:
      my_vis.write_to('/tmp/visited_final')
      print('exits:', exit_statuses)
      print('counts:', path_counts)
      break


def replay_orch(conf, input_file, enable_stdout, enable_stderr):
  addrs = sorted(conf['addrs'][:6])
  port = conf['ports'][0]

  child_pid = manage_orch(conf,
                          port,
                          'NONE',
                          addrs,
                          enable_stdout,
                          enable_stderr,
                          mode='replay',
                          input_file=input_file)
  if child_pid == -1:
    print('manage_orch failed')
    exit(1)
  else:
    os.waitpid(child_pid, 0)


if __name__ == '__main__':
  parser = argparse.ArgumentParser(
      'Deploys multiple orchestrators to run at once',
      formatter_class=argparse.RawDescriptionHelpFormatter,
      description='''
Takes in a yaml file with configuration.
- {addr} to get addr of current node
- {addrN} to get N-th node's address
- {addrs} to get all addresses
- {o_addrs} to get all addresses except node to run
- {port} to get the port nodes are listening on
---
File should have fields:
node_cmd:     <command to start a node>
client_cmd:   <command to start a client>
val_cmd:      <command to validate the files>
clean:        <command to clean up a cluster>
node_dir:	 	  <directory where node stores all hard state>
listen_ports: <list of ports where nodes are listening>
addr_range:   <list of values for last octet available for use>
# strategy:     ('random_all' | 'random_cons' | 'explore' | 'replay')
#   - 'random_cons' keep consistent, default hyperparameters
#   - 'visited'     (WIP) deployer tracks and manages choices
''')
  parser.add_argument('--yaml',
                      required=True,
                      help='config given as a yaml file')
  parser.add_argument('--parallel',
                      '-p',
                      default=1,
                      type=int,
                      help='number of orchestrators to run in parallel')
  parser.add_argument('--total',
                      '-t',
                      default=1,
                      type=int,
                      help='number of orchestrations to run total')
  parser.add_argument('--seed', '-s', default=0, type=int, help='starting seed')
  parser.add_argument('--mode',
                      choices=['rand', 'replay', 'visited'],
                      default='rand',
                      help='''
                      strategy for exploration. 
                      replay only allows total=1, parallel=1, and some input_file=1
                      ''')
  parser.add_argument('--input-file',
                      required=False,
                      help='only used for replay. the trace to replay')
  parser.add_argument('--enable-stderr',
                      action='store_true',
                      help='enables printing of orch stderr')
  parser.add_argument('--enable-stdout',
                      action='store_true',
                      help='enables printing of orch stdout')
  args = parser.parse_args()
  if args.mode == 'replay' and (args.total != 1 or args.parallel != 1 or
                                not args.input_file):
    print("replay only allows total=1, parallel=1, and some input_file=1")
    exit(1)

  conf = None
  with open(args.yaml, 'r') as fin:
    conf = yaml.safe_load(fin.read())
  status, res = validate_config(conf, args.parallel)
  if not status:
    print(res)
    exit(1)

  to_print = dict(conf)
  to_print['ports'] = [conf['ports'][0], '...', conf['ports'][-1]
                      ] if len(conf['ports']) > 1 else [conf['ports'][0]]
  to_print['addrs'] = [conf['addrs'][0], '...', conf['addrs'][-1]
                      ] if len(conf['addrs']) > 1 else [conf['addrs'][0]]
  print('successfully loaded config:', json.dumps(to_print, indent=2))
  if args.mode != 'replay':
    deploy_orchs(conf, args.mode, args.seed, args.parallel, args.total,
                 args.enable_stdout, args.enable_stderr)
  else:
    replay_orch(conf, args.input_file, args.enable_stdout, args.enable_stderr)
