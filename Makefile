CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I./include
LDFLAGS  := -lpthread
BUILD    := build

SERVER_SRCS := src/server.cpp src/thread_pool.cpp src/connection_handler.cpp \
               src/logger.cpp src/stats.cpp src/client_registry.cpp
CLIENT_SRCS := src/client.cpp src/logger.cpp

SERVER_OBJS := $(patsubst src/%.cpp, $(BUILD)/srv_%.o, $(SERVER_SRCS))
CLIENT_OBJS := $(patsubst src/%.cpp, $(BUILD)/cli_%.o, $(CLIENT_SRCS))

.PHONY: all clean run test stress interactive help

all: $(BUILD)/server $(BUILD)/client
	@echo ""
	@echo "  Build complete!"
	@echo "  Start server : ./$(BUILD)/server -p 8080 -t 4"
	@echo "  Benchmark    : ./$(BUILD)/client -t 10 -n 20"
	@echo "  Interactive  : ./$(BUILD)/client -i"
	@echo ""

$(BUILD)/server: $(SERVER_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Linked: $@"

$(BUILD)/client: $(CLIENT_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Linked: $@"

$(BUILD)/srv_%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/cli_%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) server.log
	@echo "Cleaned"

run: $(BUILD)/server
	./$(BUILD)/server -p 8080 -t 4 -v

test: $(BUILD)/client
	./$(BUILD)/client -p 8080 -t 10 -n 20

stress: $(BUILD)/client
	./$(BUILD)/client -p 8080 -t 50 -n 50

interactive: $(BUILD)/client
	./$(BUILD)/client -i -p 8080

help:
	@echo "Targets: all  clean  run  test  stress  interactive"
