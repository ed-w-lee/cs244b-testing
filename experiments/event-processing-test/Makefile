.POSIX:
CXX = g++

SRC_DIR = src
TEST_DIR = test
HDR_DIR = include

CXXFLAGS += -g -Wall -Wextra -DDEBUG -std=c++14 -I$(HDR_DIR)

all: orch testprog

orch: $(SRC_DIR)/main.cpp filter.o proxy.o
	$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp -lm filter.o proxy.o

filter.o: $(HDR_DIR)/filter.h $(SRC_DIR)/filter.cpp
	$(CXX) $(CXXFLAGS) -O -c $(SRC_DIR)/filter.cpp

proxy.o: $(HDR_DIR)/proxy.h $(SRC_DIR)/proxy.cpp
	$(CXX) $(CXXFLAGS) -O -c $(SRC_DIR)/proxy.cpp

testprog: $(TEST_DIR)/test.c
	$(CXX) -o $@ $<

clean:
	rm -f orch filter.o proxy.o testprog

cleantest:
	rm -f /tmp/orch_log.txt
	rm -rf /tmp/rafted_*
	rm -rf /tmp/filter_*
	rm -f /tmp/trace

