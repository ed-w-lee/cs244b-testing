#!/usr/bin/env python3
import sys
import json
import struct
import os

f_persist = '/tmp/raft_test_persist_'
f_out = '/tmp/rafted_tcpmvp_'

all_addrs = sys.argv[1:]

f_persist += '_'.join(all_addrs)

if os.path.isfile(f_persist):
  with open(f_persist, 'r') as fin:
    past_info = json.load(fin)
else:
  # file empty, init everything to null
  past_info = None


def read_state_file(path):
  if not os.path.exists(path):
    print("state file doesn't exist")
    exit(0)  # wait until state file exists
  with open(path, 'rb') as fin:
    buf = fin.read(9)
    curr_term, is_some = struct.unpack('>q?', buf)
    if is_some:
      buf = fin.read(8)
      num_bytes, = struct.unpack('>q', buf)
      voted_for = fin.read(num_bytes).decode('utf-8')
    else:
      voted_for = None

    buf = fin.read(8)
    first_idx, = struct.unpack('>q', buf)
    buf = fin.read(8)
    first_term, = struct.unpack('>q', buf)

  return {
      'curr_term': curr_term,
      'voted_for': voted_for,
      'first_idx': first_idx,
      'first_term': first_term,
  }


def read_logs(path):
  if not os.path.exists(path):
    print("log file doesn't exist")
    exit(0)  # wait until log file exists
  with open(path, 'rb') as fin:
    buf = fin.read(8)
    log_len, = struct.unpack('>q', buf)
    entries = []
    for i in range(log_len):
      buf = fin.read(9)
      term, is_some = struct.unpack('>q?', buf)
      if is_some:
        buf = fin.read(8)
        val, = struct.unpack('>q', buf)
      else:
        val = None
      entries.append([term, val])
  return entries


def get_state_and_logs(addr):
  my_state = read_state_file('/tmp/rafted_tcpmvp_{}/state'.format(addr))
  my_entries = read_logs('/tmp/rafted_tcpmvp_{}/entries'.format(addr))
  return {'state': my_state, 'entries': my_entries}


curr_info = {addr: get_state_and_logs(addr) for addr in all_addrs}
print(json.dumps(curr_info, sort_keys=True, indent=2))


def validate_per_addr(state, entries):
  # check that terms in log increase, and are at most current term
  print("addr validation: ", state['curr_term'])
  assert (state['curr_term'] < 1e7)
  assert (state['first_idx'] == 0)
  assert (state['first_term'] == 0)
  for entry in entries:
    assert (entry[0] <= state['curr_term'])
  for a, b in zip(entries[:-1], entries[1:]):
    assert (a[0] <= b[0])


def no_mergebacks(a, b):
  i = 0
  while True:
    if i == min(len(a), len(b)):
      return True
    if a[i] != b[i]:
      break
    i += 1

  a = a[i:]
  b = b[i:]
  return not any(el in a for el in b)


def validate_with_past(curr_info, past_info):
  for addr in all_addrs:
    past_state = past_info[addr]['state']
    past_entries = past_info[addr]['entries']

    curr_state = curr_info[addr]['state']
    curr_entries = curr_info[addr]['entries']

    print(past_state['curr_term'], curr_state['curr_term'])
    assert (past_state['curr_term'] <= curr_state['curr_term'])
    assert (no_mergebacks(past_entries, curr_entries))


def validate_curr(curr_info):
  for i, addr_i in enumerate(all_addrs):
    for j in range(i):
      addr_j = all_addrs[j]
      assert (no_mergebacks(curr_info[addr_i]['entries'],
                            curr_info[addr_j]['entries']))


for addr, sl in curr_info.items():
  validate_per_addr(sl['state'], sl['entries'])

if past_info:
  validate_with_past(curr_info, past_info)
validate_curr(curr_info)

with open(f_persist, 'w+') as fout:
  json.dump(curr_info, fout)
