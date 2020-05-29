TARGET := blackApp
SRC_DIRS := ./BlackVisitedCPP

SRCS := $(shell find $(SRC_DIRS) -name *.cpp -or -name *.c -or -name *.s)
OBJS := $(addsuffix .o,$(basename $(SRCS)))
DEPS := $(OBJS:.o=.d)

INC_DIRS := $(shell find $(SRC_DIRS) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

CPPFLAGS ?= $(INC_FLAGS) -g -Wall -Wextra -DEBUG -std=c++14

all: $(TARGET)
 
$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(LOADLIBES) $(LDLIBS)

clean:
	$(RM) $(TARGET) $(OBJS) $(DEPS)

-include $(DEPS)
