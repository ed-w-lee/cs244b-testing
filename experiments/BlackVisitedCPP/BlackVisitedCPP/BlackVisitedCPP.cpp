// BlackVisitedCPP.cpp : Defines the entry point for the application.
//
#include "BlackVisitedCPP.h"
#include "BlackVisited.h"

#include "visited.h"

int main()
{

	std::cout << "Basic test 1 : inpust list of strings" << std::endl;
	std::list<std::string> traces;
	traces.push_back("alpha");
	traces.push_back("beta");
	traces.push_back("gamma");
	Visited visitedList(10, 100);
	visitedList.start_txn(traces);
	
	std::list<std::string> traces2;
	traces2.push_back("alpha");
	traces2.push_back("beta");
	traces2.push_back("kappa");
	visitedList.start_txn(traces2);

	std::list<std::string> traces3;
	traces3.push_back("zeta");
	traces3.push_back("jotta");
	visitedList.start_txn(traces3);
	
	visitedList.end_txn();

	std::unordered_map<int, size_t>* resListCount = visitedList.get_counts();
	
	for (std::pair<int, size_t> v : *resListCount){
		std::cout << v.first << " : " << v.second << std::endl;
	}

	delete(resListCount);

	std::cout << "Save result to file visitedList.tmp" << std::endl;

	visitedList.write_paths("visitedList.tmp");

	std::cout << "Read file to other visited to check visitedList.tmp" << std::endl;
	Visited visitedListRead(10, 100);
	visitedListRead.read_paths("visitedList.tmp");
	visitedListRead.end_txn();

	std::unordered_map<int, size_t>* resListCountRead = visitedListRead.get_counts();
	
	for (std::pair<int, size_t> v : *resListCountRead){
		std::cout << v.first << " : " << v.second << std::endl;
	}

	delete(resListCountRead);

	std::cout << "Basic test 2: input only ids " << std::endl;

	Visited visitedMain(10, 100);
	visitedMain.register_child(1);
	visitedMain.register_child(2);
	visitedMain.register_child(3);
	visitedMain.end_txn();

	visitedMain.register_child(1);
	visitedMain.register_child(2);
	visitedMain.register_child(5);
	visitedMain.end_txn();

	visitedMain.register_child(7);
	visitedMain.register_child(2);
	visitedMain.register_child(5);
	visitedMain.end_txn();

	std::unordered_map<int, size_t>* resCounts = visitedMain.get_counts();
	
	for (std::pair<int, size_t> v : *resCounts){
		std::cout << v.first << " : " << v.second << std::endl;
	}

	delete(resCounts);

/*	cout << "Hello Visited." << endl;
	int MAX_MESSAGE_ID = 16;
	int WINDOW_SIZE = 5;
	BlackVisited visited(MAX_MESSAGE_ID, WINDOW_SIZE);

	for (int i = 0; i < 100; i++) {
		int val = i % 4;
		int resCount = visited.putMessage(val);
		cout << "add: " << val << ", res:" << resCount << endl;
	}
	
*/		 
	return 0;
}

