#pragma once
#include <unordered_map>
#include <iostream>

class MapTreeNode {
	int count = 0;
    std::unordered_map<int, MapTreeNode*> mapMemory;
	int idMaxSize;
public:
	MapTreeNode(int maxSize):idMaxSize(maxSize){};
	MapTreeNode* addElement(const int childId);
	MapTreeNode* getElement(const int childId);
    std::unordered_map<int, size_t>* get_counts();
    int get_count();

protected:
	int inc();
};