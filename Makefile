CXX      ?= g++
ifeq ($(origin CC),default)
  CC := gcc
endif
MODE     ?= debug
BUILD    := build/$(MODE)
EXT      := $(if $(filter Windows_NT,$(OS)),.exe)
EXE      := $(BUILD)/luxcore$(EXT)
TEST_EXE := $(BUILD)/luxtests$(EXT)
BENCH_EXE := $(BUILD)/luxbench$(EXT)
LIB       := $(BUILD)/libluxcore.a
EXAMPLE_EXE := $(BUILD)/luxexample$(EXT)
C_TEST_EXE := $(BUILD)/luxctests$(EXT)
SO_EXT    := $(if $(filter Windows_NT,$(OS)),.dll,.so)
SHARED    := $(BUILD)/libluxcore$(SO_EXT)

CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Iinclude -Iapi -MMD -MP
ifeq ($(MODE),release)
  CXXFLAGS += -O2 -DNDEBUG
else
  CXXFLAGS += -O0 -g
endif
CXXFLAGS += $(EXTRA_CXXFLAGS)

CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iapi -MMD -MP
ifeq ($(MODE),release)
  CFLAGS += -O2 -DNDEBUG
else
  CFLAGS += -O0 -g
endif
CFLAGS += $(EXTRA_CFLAGS)

SHARED_CXXFLAGS := -DLUX_BUILD_SHARED -fvisibility=hidden
SHARED_LDFLAGS  :=
ifeq ($(OS),Windows_NT)
  SHARED_LDFLAGS += -static-libstdc++ -static-libgcc
else
  SHARED_CXXFLAGS += -fPIC
endif

LIB_SRCS  := $(wildcard src/*.cpp src/*/*.cpp)
TEST_SRCS := $(wildcard tests/*.cpp)
LIB_OBJS  := $(LIB_SRCS:%.cpp=$(BUILD)/%.o)
APP_OBJS  := $(BUILD)/main.o $(LIB_OBJS)
TEST_OBJS := $(TEST_SRCS:%.cpp=$(BUILD)/%.o)
SHARED_OBJS := $(LIB_SRCS:%.cpp=$(BUILD)/shared/%.o)
C_TEST_OBJ := $(BUILD)/tests/c/test_c_api.o

.PHONY: all release run test bench lib shared example stats clean
all: $(EXE)

lib: $(LIB)

shared: $(SHARED)

example: $(EXAMPLE_EXE)
	./$(EXAMPLE_EXE) $(ARGS)

stats:
	bash tools/stats.sh

release:
	$(MAKE) MODE=release

run: $(EXE)
	./$(EXE) $(ARGS)

test: $(TEST_EXE) $(C_TEST_EXE)
	./$(TEST_EXE) $(T)
	./$(C_TEST_EXE)

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

$(LIB): $(LIB_OBJS)
	rm -f $@
	ar rcs $@ $(LIB_OBJS)

$(SHARED): $(SHARED_OBJS)
	$(CXX) -shared $(SHARED_OBJS) $(SHARED_LDFLAGS) -o $@

$(C_TEST_EXE): $(C_TEST_OBJ) $(LIB)
	$(CXX) $(C_TEST_OBJ) $(LIB) -o $@

$(EXAMPLE_EXE): $(BUILD)/examples/search.o $(LIB)
	$(CXX) $(BUILD)/examples/search.o $(LIB) -o $@

$(BUILD)/examples/%.o: examples/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(filter-out -Iinclude,$(CXXFLAGS)) -c $< -o $@

$(BUILD)/shared/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SHARED_CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

-include $(APP_OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(SHARED_OBJS:.o=.d) $(C_TEST_OBJ:.o=.d) $(BUILD)/bench/bench.d $(BUILD)/examples/search.d
