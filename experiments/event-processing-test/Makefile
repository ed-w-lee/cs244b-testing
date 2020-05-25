.POSIX:
CXX := g++

SRC_DIR := src
TEST_DIR := test
HDR_DIR := include

LIBS := filter proxy fdmap client
HDRS := $(addprefix $(HDR_DIR)/,$(addsuffix .h,$(LIBS)))
SRCS := $(addprefix $(SRC_DIR)/,$(addsuffix .cpp,$(LIBS)))
OBJS := $(addsuffix .o,$(LIBS))

CXXFLAGS += -g -Wall -Wextra -DDEBUG -std=c++14 -I$(HDR_DIR)

all: orch

orch: $(SRC_DIR)/main.cpp $(OBJS)
	$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp -lm $(filter-out $<, $^)

./%.o: $(SRC_DIR)/%.cpp $(HDR_DIR)/%.h 
	$(CXX) $(CXXFLAGS) -O -c $<

# filter.o: $(HDR_DIR)/filter.h $(SRC_DIR)/filter.cpp
# 	$(CXX) $(CXXFLAGS) -O -c $(SRC_DIR)/filter.cpp
# 
# proxy.o: $(HDR_DIR)/proxy.h $(SRC_DIR)/proxy.cpp
# 	$(CXX) $(CXXFLAGS) -O -c $(SRC_DIR)/proxy.cpp
# 
# fdmap.o: $(HDR_DIR)/fdmap.h $(SRC_DIR)/fdmap.cpp
# 	$(CXX) $(CXXFLAGS) -O -c $(SRC_DIR)/fdmap.cpp
# 
# client.o: $(HDR_DIR)/client.h $(SRC_DIR)/client.cpp
# 	$(CXX) $(CXXFLAGS) -O -c $(SRC_DIR)/client.cpp

testprog: $(TEST_DIR)/test.c
	$(CXX) -o $@ $<

clean:
	rm -f orch $(OBJS) testprog

cleantest:
	rm -f /tmp/orch_log.txt
	rm -f /tmp/raft_test_persist
	rm -rf /tmp/rafted_*
	rm -f /tmp/client_*
	rm -rf /tmp/filter_*
	rm -f /tmp/trace

.PHONY: all clean cleantest
