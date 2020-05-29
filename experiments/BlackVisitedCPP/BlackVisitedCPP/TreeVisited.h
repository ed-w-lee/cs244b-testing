#pragma once
#include <vector>

class TreeVisited {
	int count = 0;
	std::vector<TreeVisited*> idMemory;
	int idMaxSize;
public:
	TreeVisited(int maxSize);
	int addPath(const std::vector<int>& window, int startId, int maxSize);

private:
	TreeVisited* getChild(const int childId);
	const int inc();
};