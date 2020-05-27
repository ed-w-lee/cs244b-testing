#!/usr/bin/env python3
import socket
import sys

all_addrs = sys.argv[1:]

curr_addr = all_addrs[0]
read_val = 0
counter = 1
stored = None

ERR_LIMIT = 20
err_count = 0

it = 0
while True:
  it += 1
  if it % 3 == 0:
    to_send = 0
  elif it % 3 == 1:
    to_send = counter // 2
  else:
    to_send = counter
    counter += 1
  to_send_str = (str(to_send) + '\n').encode()

  with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind(('127.0.0.1', 0))
    try:
      s.connect((curr_addr, 4242))
      print('sending', to_send_str)
      s.sendall(to_send_str)
      data = s.recv(1024)
    except OSError as msg:
      print("couldn't maintain connection: {}".format(msg))
      err_count += 1
      curr_addr = all_addrs[it % 3]
      s.close()
      if err_count > ERR_LIMIT:
        sys.exit(1)
      continue
    err_count = 0
    s.close()

    if len(data) == 0:
      print('disconnected')
      continue

    data_str = data.decode('utf-8')
    if data_str.startswith('no'):
      print('no leader')
      continue
    elif data_str.startswith('leader is likely'):
      print('redirect')
      curr_addr = data_str.strip().split()[-1]
    elif to_send == 0:
      new_stored = int(data_str.strip())
      print('reading val: {}'.format(new_stored))
      assert (not stored or new_stored >= stored)
      stored = new_stored
      if stored > counter:
        counter = stored + 1
    else:
      ret = int(data_str.strip())
      print('send {} and received {}'.format(to_send, ret))
      if ret == 0:
        print('stored val is greater than sent {}'.format(to_send))
      else:
        assert (ret >= to_send)
        assert (not stored or to_send >= stored)
        stored = to_send
