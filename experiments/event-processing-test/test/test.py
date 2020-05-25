#!/usr/bin/env python3

import argparse
import os
import socket
# import asyncio

# def read_file(file):
# 	with open(file, 'r', buffering=100) as fin:
# 		v = fin.read()
# 		if v:
# 			return int(v.strip())
# 		else:
# 			return 1000
# 
# def write_file(file, to_write):
# 	with open(file, 'w', buffering=2) as f:
# 		f.write(str(to_write))
# 		f.flush()
# 		os.fsync(f.fileno())
# 		f.write(str(to_write))
# 
# parser = argparse.ArgumentParser(description="test the filtering with disk ops and network ops")
# parser.add_argument('diskdir', nargs='?', default='/tmp/our_cs244b_test_13245646', help='directory where disk ops will be located')
# parser.add_argument('networkaddr', nargs='?', default=None, help='address where network messages will be sent/received')
# 
# args = parser.parse_args()
# 
# # files = [os.path.join(args.diskdir, str(i)) for i in range(5)]
# # for f in files:
# # 	if not os.path.exists(f):
# # 		os.mknod(f)
# # 
# # for _ in range(2):
# # 	vals = [read_file(f) for f in files]
# # 	print(vals)
# # 	[write_file(f, vals[i] + i * 100) for i,f in enumerate(files)]
# 
# async def handle_client(reader, writer):
# 	request = (await reader.read(255)).decode('utf-8')
# 	res = str(request)
# 	writer.write(res.encode('utf-8'))
# 	await writer.drain()
# 	writer.close()
# 
# # async def run_server():
# # 	while True:
# # 		client, _ = await loop.sock_accept(s)
# # 		loop.create_task(handle_client(client))
# 
# loop = asyncio.get_event_loop()
# loop.create_task(asyncio.start_server(handle_client, '127.0.0.10', 4242))
# loop.run_forever()

os.rename("/tmp/blah", "/tmp/blah2")
