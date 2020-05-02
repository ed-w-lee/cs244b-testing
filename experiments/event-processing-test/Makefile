.POSIX:
CXX = g++

FILE_DIR = our_cs244b_test_13245646
SRC_DIR = src
HDR_DIR = include

CXXFLAGS += -g -Wall -Wextra -DDEBUG -std=c++14 -I$(HDR_DIR)

all: orch

install:
	cp -r $(FILE_DIR) /tmp

orch: $(SRC_DIR)/main.cpp filter.o proxy.o
	$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp -lm filter.o proxy.o

filter.o: $(HDR_DIR)/filter.h $(SRC_DIR)/filter.cpp
	$(CXX) $(CXXFLAGS) -O -c $(SRC_DIR)/filter.cpp

proxy.o: $(HDR_DIR)/proxy.h $(SRC_DIR)/proxy.cpp
	$(CXX) $(CXXFLAGS) -O -c $(SRC_DIR)/proxy.cpp

# test: test/testprog.cpp
# 	$(CXX) $(LDFLAGS) $(CXXFLAGS) -o testprog $<

clean:
	rm -f orch filter.o proxy.o testprog
	rm -rf /tmp/$(FILE_DIR)

