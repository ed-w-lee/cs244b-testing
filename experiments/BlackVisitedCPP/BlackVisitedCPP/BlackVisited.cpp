#include "BlackVisited.h"

BlackVisited::BlackVisited(int maxMemory, int windowSize)
{
	maxMemoryId = maxMemory;
	slideWindowSize = windowSize;
	rootNode = new TreeVisited(maxMemoryId);
	slidingWindow.resize(slideWindowSize);
	currentWindowId = -1;
}

// use only 1+ (Natural numbers) for input messages
int BlackVisited::putMessage(int messageId)
{
	processedItemsCount++;
	if (messageId >= maxMemoryId) {
		return 0;
	}

	currentWindowId = (currentWindowId + 1) % slideWindowSize;
	slidingWindow[currentWindowId] = messageId;
	int resultCount = 0;

	if (processedItemsCount > slideWindowSize) {
		resultCount = rootNode->addPath(slidingWindow, (currentWindowId + 1) % slideWindowSize, slideWindowSize);
	}

	return resultCount;
}

