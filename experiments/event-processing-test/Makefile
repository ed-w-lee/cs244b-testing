.POSIX:
CXX = g++

FILE_DIR = our_cs244b_test_13245646
SRC_DIR = src
FILTER_SRCS = filter.cpp redir_filter.cpp
HDR_DIR = include

CXXFLAGS += -g -Wall -Wextra -DDEBUG -std=c++14 -I$(HDR_DIR)

all: redir_filter install

install:
	cp -r $(FILE_DIR) /tmp

redir_filter: filter.o
	$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ $(SRC_DIR)/redir_filter.cpp -lm filter.o

filter.o: $(HDR_DIR)/filter.h $(SRC_DIR)/filter.cpp
	$(CXX) $(CXXFLAGS) -O -c $(SRC_DIR)/filter.cpp

# test: test/testprog.cpp
# 	$(CXX) $(LDFLAGS) $(CXXFLAGS) -o testprog $<

clean:
	rm -f redir_filter filter.o testprog
	rm -rf /tmp/$(FILE_DIR)

