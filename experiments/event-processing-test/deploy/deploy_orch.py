#!/usr/bin/env python3
import argparse
import yaml
import json
import os
import sys
import time
import errno
import shlex
import subprocess
from collections import deque

__location__ = os.path.realpath(
    os.path.join(os.getcwd(), os.path.dirname(__file__)))


def validate_config(conf, p):
  print('validating...')
  if not conf:
    return (False, "conf doesn't exist: {}".format(conf))
  fields = [
      'clean', 'node_cmd', 'client_cmd', 'val_cmd', 'addr_range', 'node_dir',
      'listen_port'
  ]
  for field in fields:
    if field not in conf or not conf[field]:
      return (False, 'missing field {}'.format(field))
  addrs = []
  for rng in conf['addr_range']:
    if isinstance(rng, list):
      if len(rng) != 2:
        return (False, 'range {} has too many fields'.format(rng))
      else:
        for a in range(rng[0], rng[1] + 1):
          addrs.append('127.0.0.{}'.format(a))
    else:
      addrs.append('127.0.0.{}'.format(rng))

  addrs = sorted(list(set(addrs)))
  if len(addrs) < 6 * p:
    return (False,
            'too few addrs ({}) for {} parallel clusters, minimum is {}'.format(
                len(addrs), p, 6 * p))
  del conf['addr_range']
  conf['addrs'] = addrs
  return (True, conf)


def manage_orch(conf, seed, addrs):
  '''
  Manages an orch instance. Runs in a separate process in case we need to
  communicate with the instance.
  Forks off an orch with the given config and seed, and then waits for completion
  '''
  addrs.sort()
  proxy_addrs = addrs[::2]
  node_addrs = addrs[1::2]
  print('attempting to start orch with seed {}'.format(seed))
  print('proxy_addrs: {}'.format(proxy_addrs))
  print('node_addrs: {}'.format(node_addrs))

  format_nodes = {
      'addr0': node_addrs[0],
      'addr1': node_addrs[1],
      'addr2': node_addrs[2],
      'addrs': ' '.join(node_addrs),
      'o_addrs': '{o_addrs}',
      'addr': '{addr}'
  }

  # start by cleaning up seed just in case
  subprocess.run([os.path.join(__location__, 'cleanup.sh'), str(seed)])
  clean_cmd = shlex.split(conf['clean'].format(**format_nodes))
  res = subprocess.run(clean_cmd)
  print('finished cleaning with {} using {}'.format(res.returncode, clean_cmd))

  delim = '#'  # just use as delimiter (assume typical commands don't include #)
  conf['node_cmd'] = delim.join(
      shlex.split(conf['node_cmd'].format(**format_nodes)))
  conf['client_cmd'] = delim.join(
      shlex.split(conf['client_cmd'].format(**format_nodes)))
  conf['val_cmd'] = delim.join(
      shlex.split(conf['val_cmd'].format(**format_nodes)))
  command = '''
            ./orch --mode rand --seed '{seed}'
            --node '{node_cmd}' 
            --client '{client_cmd}' 
            --val '{val_cmd}'
            --node-dir '{node_dir}'
            --listen-port '{listen_port}'
            '''
  command = command.format(seed=seed, **conf)
  command = command.format(**format_nodes)
  command += '''
             --old-addrs '{node_addrs}'
             --new-addrs '{proxy_addrs}'
             '''.format(node_addrs=' '.join(node_addrs),
                        proxy_addrs=' '.join(proxy_addrs))
  command = shlex.split(command)
  trace_file = '/tmp/trace_{}'.format(seed)
  print('attempting to run {}'.format(command))
  try:
    child_pid = os.fork()
    if child_pid == 0:
      os.setpgid(0, 0)
      # devnull = os.open(os.devnull, os.O_WRONLY)
      out = os.open(trace_file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC)
      devnull = os.open('/dev/null', os.O_WRONLY)
      os.dup2(out, 1)
      os.dup2(devnull, 2)
      exit(os.execv(command[0], command))
    pid, status = os.waitpid(child_pid, 0)
    if os.WIFEXITED(status) and os.WEXITSTATUS(status) < 100:
      # clean up
      print('exit status:', os.WEXITSTATUS(status))
      os.system(conf['clean'])
      return os.WEXITSTATUS(status)
    print('was not an exit:', status)
    print('WIFEXITED:', os.WIFEXITED(status))
    print('WEXITSTATUS:', os.WEXITSTATUS(status))
    print("WIFSIGNALED:", os.WIFSIGNALED(status))
    print("WTERMSIG:", os.WTERMSIG(status))
    return 100
  except OSError as e:
    print('manage_orch failed')
    return 100


def start_new_manage_orch(conf, addrs, seed):
  child_pid = os.fork()
  if child_pid == 0:
    exit(manage_orch(conf, seed, addrs))
  return child_pid


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


def deploy_orchs(conf, seed, parallel, total):
  print('deploying...')
  num_rounds = 0
  num_completed = 0

  free_addrs = deque(conf['addrs'])
  child_status = {}
  for _ in range(min(total, parallel)):
    # take next 6 addrs
    child_addrs = [free_addrs.popleft() for _ in range(6)]
    child_pid = start_new_manage_orch(conf, child_addrs, seed)
    child_status[child_pid] = (seed, child_addrs)
    num_rounds += 1
    seed += 1

  while True:
    try:
      pid, status = os.waitpid(-1, 0)
    except OSError as e:
      print("unexpected waitpid error: {}".format(e))
      break
    if not os.WIFEXITED(status):
      print("unexpected status, didn't exit: {}".format(status))
      wait_for_children_to_finish()
      exit(1)
    exit_status = os.WEXITSTATUS(status)
    if exit_status >= 100:
      # likely manage_orch exited
      print('manage_orch {} exited unexpectedly: {}'.format(pid, exit_status))
      wait_for_children_to_finish()
      exit(1)
    elif exit_status == 1:
      # orch failed. we'd want to keep running, but for now just exit
      print('orch {} failed'.format(pid))
      wait_for_children_to_finish()
      exit(1)

    child_seed, child_addrs = child_status[pid]
    print('manage_orch {} seed {} exited with {}'.format(
        pid, child_seed, exit_status))
    del child_status[pid]
    for a in child_addrs:
      free_addrs.append(a)
    if exit_status == 0:
      # orch succeeded, just clean up records of this
      subprocess.run(
          [os.path.join(__location__, 'cleanup.sh'),
           str(child_seed)])
      clean_cmd = shlex.split(
          conf['clean'].format(addrs=' '.join(sorted(child_addrs)[1::2])))
      res = subprocess.run(clean_cmd)

    time.sleep(1)
    num_completed += 1
    if num_rounds < total:
      num_rounds += 1
      child_addrs = [free_addrs.popleft() for _ in range(6)]
      child_pid = start_new_manage_orch(conf, child_addrs, seed)
      child_status[child_pid] = (seed, child_addrs)
      seed += 1
    elif num_completed >= total:
      break


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
---
File should have fields:
node_cmd:    <command to start a node>
client_cmd:  <command to start a client>
val_cmd:     <command to validate the files>
clean:       <command to clean up a cluster>
node_dir:		 <directory where node stores all hard state>
listen_port: <port where nodes are listening>
addr_range:  <list of values for last octet available for use>
strategy:    ('random_all' | 'random_cons' | 'explore' | 'replay')
  - 'random_all'  (WIP) randomize hyperparameters
  - 'random_cons' keep consistent, default hyperparameters
  - 'explore'     (WIP) deployer tracks and manages choices
  - 'replay'      (WIP) replay some run record
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
  args = parser.parse_args()

  conf = None
  with open(args.yaml, 'r') as fin:
    conf = yaml.safe_load(fin.read())
  status, res = validate_config(conf, args.parallel)
  if not status:
    print(res)
    exit(1)

  print('successfully loaded config:', json.dumps(conf, indent=2))
  deploy_orchs(conf, args.seed, args.parallel, args.total)
