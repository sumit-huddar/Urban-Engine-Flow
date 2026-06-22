# Urban Flow Engine — build for the from-scratch max-flow / min-cut binary.
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
BIN_DIR   := bin
BIN       := $(BIN_DIR)/flow
TEST_BIN  := $(BIN_DIR)/run_tests

.PHONY: all clean test run-example

all: $(BIN)

$(BIN): src/main.cpp src/flow_network.hpp src/json.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp

$(TEST_BIN): tests/run_tests.cpp src/flow_network.hpp src/json.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ tests/run_tests.cpp

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

test: $(TEST_BIN)
	./$(TEST_BIN)

run-example: $(BIN)
	./$(BIN) examples/clrs_network.json

clean:
	rm -rf $(BIN_DIR)
