#include "TreeVisited.h"

TreeVisited::TreeVisited(const int maxSize)
{
	idMaxSize = maxSize;
	idMemory.resize(idMaxSize);
}

// use only 1+ (Natural numbers) for input messages
int TreeVisited::addPath(const std::vector<int>& window, int startId, int maxSize)
{
	TreeVisited* nowNode = this;
	for (int i = 0; i < maxSize; i++) {
		int nowId = window[startId];
		nowNode = nowNode->getChild(nowId);
		startId = (startId + 1) % maxSize;
	}
	
	return nowNode->inc();
}

TreeVisited* TreeVisited::getChild(const int childId)
{
	if (idMemory[childId] == NULL) {
		idMemory[childId] = new TreeVisited(idMaxSize);
	}
	return idMemory[childId];
}

const int TreeVisited::inc() {
	return ++count;
}