CC = gcc
CFLAGS = -Wall -Wextra -O2 -fPIC -std=c11
CFLAGS_DEBUG = -Wall -Wextra -g -DDEBUG -std=c11
INCLUDES = -I./include
LIBS = -lpthread

# Get git SHA for build metadata
GIT_SHA := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
CFLAGS += -DGIT_SHA=\"$(GIT_SHA)\"

# Source files
SOURCES = src/main.c src/packet.c src/logger.c src/thread_pool.c src/buffer.c src/parser.c src/socket_handler.c src/metrics.c src/regression.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = build/packet_analyzer

# Default target
all: $(TARGET)

# Debug build
debug: CFLAGS = $(CFLAGS_DEBUG)
debug: clean $(TARGET)

# Build target
$(TARGET): $(OBJECTS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)
	@echo "Build complete: $@"

# Object files
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Clean build artifacts
clean:
	rm -rf $(OBJECTS) $(TARGET) build/*.o

# Run the analyzer (requires sudo)
run: $(TARGET)
	sudo $(TARGET) -i eth0 -d

# Run with custom interface
run-if:
	@read -p "Enter interface name (default: eth0): " iface; \
	sudo $(TARGET) -i $${iface:-eth0} -d

# Help target
help:
	@echo "Available targets:"
	@echo "  all       - Build the packet analyzer (default)"
	@echo "  debug     - Build with debug symbols"
	@echo "  clean     - Remove build artifacts"
	@echo "  run       - Build and run on eth0 (requires sudo)"
	@echo "  run-if    - Build and run with custom interface (requires sudo)"
	@echo "  test      - Run unit tests"
	@echo "  test-regression - Run regression validation tests"
	@echo "  help      - Display this message"

# Unit tests
TEST_SOURCES = src/packet.c src/logger.c src/thread_pool.c src/buffer.c src/parser.c src/socket_handler.c src/metrics.c src/regression.c
TEST_BASIC_TARGET = build/test_basic
TEST_REGRESSION_TARGET = build/test_regression

test: test-basic test-regression

test-basic: $(TEST_BASIC_TARGET)
	./$(TEST_BASIC_TARGET)

$(TEST_BASIC_TARGET): tests/test_basic.c $(TEST_SOURCES)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)
	@echo "Built: $@"

test-regression: $(TEST_REGRESSION_TARGET)
	./$(TEST_REGRESSION_TARGET)

$(TEST_REGRESSION_TARGET): tests/test_regression.c $(TEST_SOURCES)
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)
	@echo "Built: $@"

.PHONY: all debug clean run run-if help test test-basic test-regression
