CXX ?= g++
SAN ?=

CXXFLAGS := -std=c++17 -Wall -Wextra -g -Iinclude -Ikernel/virtual_device -pthread
LDFLAGS := -pthread

ifeq ($(SAN),thread)
CXXFLAGS += -O1 -fsanitize=thread
LDFLAGS += -fsanitize=thread
BUILD := build-tsan
else ifeq ($(SAN),address)
CXXFLAGS += -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
BUILD := build-asan
else
CXXFLAGS += -O2
BUILD := build
endif

HEADERS := $(wildcard include/*/*.h kernel/virtual_device/*.h tests/*.h)

LIB := $(BUILD)/libframework.a
LIB_SRCS := $(filter-out src/cli/main.cpp,$(wildcard src/*/*.cpp src/*/*/*.cpp))
LIB_OBJS := $(patsubst src/%.cpp,$(BUILD)/obj/%.o,$(LIB_SRCS))

TEST_SRCS := $(wildcard tests/unit/*.cpp tests/integration/*.cpp tests/concurrency/*.cpp tests/failure_injection/*.cpp)
TEST_BINS := $(patsubst tests/%.cpp,$(BUILD)/tests/%,$(TEST_SRCS))

APPS := $(BUILD)/bin/device_monitor $(BUILD)/bin/temperature_reader $(BUILD)/bin/event_subscriber \
        $(BUILD)/bin/char_device_client $(BUILD)/bin/benchmark

.PHONY: all lib apps tests run-tests driver-test kernel clean

all: apps tests

lib: $(LIB)

apps: $(APPS)

tests: $(TEST_BINS)

$(LIB): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(BUILD)/obj/%.o: src/%.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/bin/device_monitor: src/cli/main.cpp $(LIB) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LDFLAGS) -o $@

$(BUILD)/bin/temperature_reader: examples/temperature_reader/main.cpp $(LIB) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LDFLAGS) -o $@

$(BUILD)/bin/event_subscriber: examples/event_subscriber/main.cpp $(LIB) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LDFLAGS) -o $@

$(BUILD)/bin/char_device_client: examples/char_device_client/main.cpp $(LIB) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LDFLAGS) -o $@

$(BUILD)/bin/benchmark: benchmarks/benchmark.cpp $(LIB) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LDFLAGS) -o $@

$(BUILD)/tests/%: tests/%.cpp tests/TestHelper.h $(LIB) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Itests $< $(LIB) $(LDFLAGS) -o $@

$(BUILD)/tests/driver/driver_test: tests/driver/driver_test.cpp tests/TestHelper.h $(LIB) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wno-unused-result -Wno-stringop-overread -Wno-stringop-overflow -Itests $< $(LIB) $(LDFLAGS) -o $@

driver-test: $(BUILD)/tests/driver/driver_test

run-tests: tests
	@./scripts/run_tests.sh $(BUILD)

kernel:
	$(MAKE) -C kernel/virtual_device

clean:
	rm -rf build build-tsan build-asan
	-$(MAKE) -C kernel/virtual_device clean
