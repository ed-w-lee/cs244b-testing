#pragma once
#include <iostream>
#include <unordered_map>

class MapTreeNode {
  int old_count = 0;
  int count = 0;
  std::unordered_map<int, MapTreeNode *> mapMemory;
  int idMaxSize;

public:
  MapTreeNode(int maxSize) : idMaxSize(maxSize){};
  MapTreeNode *addElement(const int childId, bool reading = false,
                          size_t to_inc = 1);
  MapTreeNode *getElement(const int childId);
  std::unordered_map<int, size_t> *get_counts();
  int get_count();
  int get_old_count();
  std::pair<std::unordered_map<int, MapTreeNode *>::iterator,
            std::unordered_map<int, MapTreeNode *>::iterator>
  get_iterators();

protected:
  int inc(size_t to_inc);
  void inc_old(size_t to_inc);
};