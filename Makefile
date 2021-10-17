SRC := src
INC := include
BLD := build

PRJ := test_virtual_tcp

GCC := g++ -std=c++17 -Wall -O2
DBG := -g3 -O0
CFLAGS := -I$(INC) $(DBG)
LIBS := -pthread

SRCS := $(wildcard $(SRC)/*.cpp)
OBJS := $(patsubst $(SRC)/%.cpp,$(BLD)/%.o,$(SRCS))

.PHONY: all
all: $(BLD)/$(PRJ)

.PHONY: run
run: $(BLD)/$(PRJ)
	rlwrap gdb $<

$(BLD)/$(PRJ): $(OBJS)
	$(GCC) $(LIBS) -o $@ $^

$(BLD)/%.o: $(SRC)/%.cpp
	$(GCC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm $(BLD)/*

