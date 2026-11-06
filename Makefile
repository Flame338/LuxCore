CXX      ?= g++
MODE     ?= debug
BUILD    := build/$(MODE)
EXT      := $(if $(filter Windows_NT,$(OS)),.exe)
EXE      := $(BUILD)/luxcore$(EXT)
TEST_EXE := $(BUILD)/luxtests$(EXT)
BENCH_EXE := $(BUILD)/luxbench$(EXT)

CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Iinclude -MMD -MP
ifeq ($(MODE),release)
  CXXFLAGS += -O2 -DNDEBUG
else
  CXXFLAGS += -O0 -g
endif
CXXFLAGS += $(EXTRA_CXXFLAGS)

LIB_SRCS  := $(wildcard src/*.cpp src/*/*.cpp)
TEST_SRCS := $(wildcard tests/*.cpp)
LIB_OBJS  := $(LIB_SRCS:%.cpp=$(BUILD)/%.o)
APP_OBJS  := $(BUILD)/main.o $(LIB_OBJS)
TEST_OBJS := $(TEST_SRCS:%.cpp=$(BUILD)/%.o)

.PHONY: all release run test bench clean
all: $(EXE)

release:
	$(MAKE) MODE=release

run: $(EXE)
	./$(EXE) $(ARGS)

test: $(TEST_EXE)
	./$(TEST_EXE) $(T)

bench: $(BENCH_EXE)
	./$(BENCH_EXE) $(ARGS)

clean:
	rm -rf build

$(EXE): $(APP_OBJS)
	$(CXX) $(APP_OBJS) -o $@

$(TEST_EXE): $(TEST_OBJS) $(LIB_OBJS)
	$(CXX) $(TEST_OBJS) $(LIB_OBJS) -o $@

$(BENCH_EXE): $(BUILD)/bench/bench.o $(LIB_OBJS)
	$(CXX) $(BUILD)/bench/bench.o $(LIB_OBJS) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(APP_OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(BUILD)/bench/bench.d
