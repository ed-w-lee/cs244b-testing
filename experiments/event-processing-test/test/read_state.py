#!/usr/bin/env python3
import sys
import os
import struct

def read_state_file(path):
	if not os.path.exists(path):
		print("state file doesn't exist")
		exit(0) # wait until state file exists
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


print(read_state_file(sys.argv[1]))
