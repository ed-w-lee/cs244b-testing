#pragma once
#include "TreeVisited.h"
#include <vector>

class BlackVisited {
	int maxMemoryId, slideWindowSize;
	TreeVisited* rootNode = NULL;
	std::vector<int> slidingWindow;
	int currentWindowId;
	int processedItemsCount = 0;

public:
	BlackVisited(int maxMemory, int windowSize);
	int putMessage(int id);
};