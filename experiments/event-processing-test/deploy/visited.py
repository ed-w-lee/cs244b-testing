#!/usr/bin/env python3
import argparse
import json
import os


class Node:

  def __init__(self):
    self.children = {}
    self.count = 0

  def add_child(self, key):
    if key not in self.children:
      self.children[key] = Node()
    return self.children[key]

  def inc_count(self, count):
    self.count += count

  def get_children(self):
    if len(self.children) == 0:
      return None
    else:
      return {k: v.get_children() for k, v in self.children.items()}

  def add_other(self, other, remap, chain_len, depth):
    children_o = other.children
    if children_o:
      for k_o, node_o in children_o.items():
        k_op = remap[k_o] if (depth + 1) < chain_len else k_o
        if k_op not in self.children:
          self.add_child(k_op)
        self.children[k_op].add_other(node_o, remap, chain_len, depth + 1)
    self.inc_count(other.count)

  def write_to(self, fout):
    for k, n in self.children.items():
      fout.write(k + ';' + str(n.count) + ';0\n')
      n.write_to(fout)
      fout.write('#\n')

  def validate(self, key_map, chain_len, depth):
    if (depth + 1) < chain_len:
      # children should be converted
      for k, v in self.children.items():
        # print(depth + 1, k)
        assert (k in key_map)
        v.validate(key_map, chain_len, depth + 1)


class VisitedTree:

  def __init__(self):
    self.root = Node()
    self.key_map = {}
    self.chain_len = None
    self.max_id = -1

  def get_from_file(self, file, relative=False):
    if not os.path.isfile(file):
      return False

    with open(file, 'r') as fin:
      self.chain_len = int(next(fin))
      num_lines = int(next(fin))
      for _ in range(num_lines):
        l = next(fin)
        node, id = tuple(l.strip().split(';'))
        self.key_map[node] = id
        self.max_id = max(self.max_id, int(id))

      assert (len(next(fin).strip()) == 0)

      depth = [self.root]
      curr_node = depth[0]
      for l in fin:
        if l.strip() == '#':
          depth.pop()
          if len(depth) == 0:
            curr_node = None
          else:
            curr_node = depth[-1]
        else:
          node, new_count, old_count = tuple(l.strip().split(';'))
          new_count = int(new_count)
          old_count = int(old_count)
          curr_node = curr_node.add_child(node)
          if relative:
            curr_node.inc_count(new_count - old_count)
          else:
            curr_node.inc_count(new_count)
          depth.append(curr_node)
    return True

  def add_other(self, other):
    assert (isinstance(other, VisitedTree))
    assert (self.chain_len is None or other.chain_len == self.chain_len)
    self.chain_len = other.chain_len
    # allocate any new required keys
    key_remap = {}
    for k, v in other.key_map.items():
      if k not in self.key_map:
        self.max_id += 1
        self.key_map[k] = str(self.max_id)
      key_remap[v] = self.key_map[k]

    print(key_remap)
    self.root.add_other(other.root, key_remap, self.chain_len, 0)

  def write_to(self, outfile):
    with open(outfile, 'w') as fout:
      fout.write(str(self.chain_len) + '\n')
      fout.write(str(len(self.key_map)) + '\n')
      for k, v in self.key_map.items():
        fout.write(k + ';' + v + '\n')
      fout.write('\n')
      fout.flush()

      # write the tree
      self.root.write_to(fout)

  def validate(self):
    my_key_map = {}
    for k, v in self.key_map.items():
      my_key_map[v] = k
    self.root.validate(my_key_map, self.chain_len, 0)

  def __str__(self):
    return 'key_map: {}, tree: {}'.format(
        json.dumps(self.key_map, sort_keys=True, indent=2),
        json.dumps(self.root.get_children(), sort_keys=True, indent=2))


if __name__ == "__main__":
  argparse = argparse.ArgumentParser(
      description="Parses a visited tree and prints it out formatted prettily")
  argparse.add_argument('--input-file',
                        '-i',
                        help='File with the visited tree',
                        required=True)
  argparse.add_argument('--validate',
                        '-v',
                        help='validate the input tree',
                        action='store_true')
  argparse.add_argument('--add-file',
                        '-a',
                        help='File with the visited tree to add',
                        required=False)

  args = argparse.parse_args()
  vis = VisitedTree()
  vis.get_from_file(args.input_file)
  # print(str(vis))

  vis.write_to("/tmp/blah")

  if args.validate:
    vis.validate()

  if args.add_file:
    vis2 = VisitedTree()
    vis2.get_from_file(args.add_file)
    # print(str(vis2))

    vis.add_other(vis2)
    # print(str(vis))