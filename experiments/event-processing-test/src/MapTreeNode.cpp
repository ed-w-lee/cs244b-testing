#include "MapTreeNode.h"

MapTreeNode *MapTreeNode::addElement(const int childId, bool reading,
                                     size_t to_inc) {
  MapTreeNode *child = NULL;
  if (mapMemory.find(childId) == mapMemory.end()) {
    child = new MapTreeNode(idMaxSize);
    mapMemory[childId] = child;
  } else {
    child = mapMemory[childId];
  }
  if (reading) {
    child->inc_old(to_inc);
  } else {
    child->inc(to_inc);
  }
  return child;
}

int MapTreeNode::inc(size_t to_inc) {
  count += to_inc;
  return count;
}

void MapTreeNode::inc_old(size_t to_inc) {
  count += to_inc;
  old_count += to_inc;
}

MapTreeNode *MapTreeNode::getElement(const int childId) {
  if (mapMemory.find(childId) == mapMemory.end()) {
    return NULL;
  }

  return mapMemory[childId];
}

std::unordered_map<int, size_t> *MapTreeNode::get_counts() {
  std::unordered_map<int, size_t> *res = new std::unordered_map<int, size_t>();
  for (std::pair<int, MapTreeNode *> node : mapMemory) {
    res->insert({{node.first, node.second->get_count()}});
  }
  return res;
}

int MapTreeNode::get_count() { return count; }

int MapTreeNode::get_old_count() { return old_count; }

std::pair<std::unordered_map<int, MapTreeNode *>::iterator,
          std::unordered_map<int, MapTreeNode *>::iterator>
MapTreeNode::get_iterators() {
  return std::make_pair(mapMemory.begin(), mapMemory.end());
}