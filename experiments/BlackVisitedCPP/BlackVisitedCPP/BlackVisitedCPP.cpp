// BlackVisitedCPP.cpp : Defines the entry point for the application.
//
#include "BlackVisitedCPP.h"
#include "BlackVisited.h"

using namespace std;

int main()
{
	cout << "Hello Visited." << endl;
	int MAX_MESSAGE_ID = 16;
	int WINDOW_SIZE = 5;
	BlackVisited visited(MAX_MESSAGE_ID, WINDOW_SIZE);

	for (int i = 0; i < 100; i++) {
		int val = i % 4;
		int resCount = visited.putMessage(val);
		cout << "add: " << val << ", res:" << resCount << endl;
	}
	
		 
	return 0;
}
