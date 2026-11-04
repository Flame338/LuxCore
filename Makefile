CXX      ?= g++
MODE     ?= debug
BUILD    := build/$(MODE)
EXE      := $(BUILD)/luxcore$(if $(filter Windows_NT,$(OS)),.exe)

CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Iinclude -MMD -MP
ifeq ($(MODE),release)
  CXXFLAGS += -O2 -DNDEBUG
else
  CXXFLAGS += -O0 -g
endif

# main.cpp plus anything under src/ (one or two folders deep) is picked up automatically.
SRCS := main.cpp $(wildcard src/*.cpp src/*/*.cpp)
OBJS := $(SRCS:%.cpp=$(BUILD)/%.o)

.PHONY: all release run clean
all: $(EXE)

release:
	$(MAKE) MODE=release

run: $(EXE)
	./$(EXE)

clean:
	rm -rf build

$(EXE): $(OBJS)
	$(CXX) $(OBJS) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)
