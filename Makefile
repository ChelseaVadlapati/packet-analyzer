CC = gcc
CFLAGS = -Wall -Wextra -O2 -fPIC
CFLAGS_DEBUG = -Wall -Wextra -g -DDEBUG
INCLUDES = -I./include
LIBS = -lpthread

# Source files
SOURCES = src/main.c src/packet.c src/logger.c src/thread_pool.c src/buffer.c src/parser.c src/socket_handler.c
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
	@echo "  help      - Display this message"

.PHONY: all debug clean run run-if help
