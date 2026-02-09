# Getting Started with the Network Packet Analyzer

## Quick Start

### 1. Build the Project
```bash
cd /Users/chelseavadlapati/packet_analyzer
make
```

### 2. Run the Application
For packet capture on Linux:
```bash
sudo ./build/packet_analyzer -i eth0
```

For testing without actual packet capture:
```bash
./build/test_basic
```

## Project Overview

The Network Packet Analyzer is a high-performance packet capture and analysis tool written in C. It demonstrates:

- **Low-level Network Programming**: Direct socket programming with TCP/IP
- **Concurrent Processing**: Multi-threaded packet handling with thread pools
- **Memory Management**: Circular buffers and safe memory allocation
- **Protocol Analysis**: Parsing of Ethernet, IPv4, TCP, and UDP headers
- **Logging & Debugging**: Comprehensive logging system with multiple levels

## Directory Structure

```
packet_analyzer/
├── src/                    # Core implementation files
│   ├── main.c             # Application entry point and main loop
│   ├── packet.c           # Packet structure and management
│   ├── socket_handler.c   # Network interface and socket operations
│   ├── thread_pool.c      # Multi-threaded work queue
│   ├── logger.c           # Logging utilities
│   ├── parser.c           # Protocol parsing functions
│   └── buffer.c           # Circular buffer implementation
│
├── include/               # Header files (API definitions)
│   ├── packet.h
│   ├── socket_handler.h
│   ├── thread_pool.h
│   ├── logger.h
│   ├── parser.h
│   └── buffer.h
│
├── tests/                 # Test files
│   └── test_basic.c      # Unit tests for core components
│
├── build/                 # Compiled binaries (created after make)
│   ├── packet_analyzer   # Main application
│   └── test_basic        # Test binary
│
├── Makefile              # Build configuration
├── README.md             # Full documentation
└── GETTING_STARTED.md    # This file
```

## Key Components

### Socket Handler (`src/socket_handler.c`)
Manages network interface access and packet reception. Abstracts platform differences between Linux (AF_PACKET) and macOS (BPF).

**Key Functions:**
- `socket_init()` - Initialize socket configuration
- `socket_bind_raw()` - Create raw socket for packet capture
- `socket_enable_promiscuous()` - Enable promiscuous mode on interface
- `socket_receive_packet()` - Receive raw packets from network

### Thread Pool (`src/thread_pool.c`)
Distributes packet processing across multiple worker threads for performance.

**Key Functions:**
- `thread_pool_create()` - Create pool with N worker threads
- `thread_pool_enqueue()` - Add packet to processing queue
- `thread_pool_destroy()` - Gracefully shutdown pool
- `thread_pool_get_processed_count()` - Get statistics

### Packet Parser (`src/parser.c`)
Parses network protocol headers (Ethernet, IPv4, TCP, UDP).

**Key Functions:**
- `parse_ethernet_header()` - Parse Layer 2 header
- `parse_ipv4_header()` - Parse Layer 3 header
- `parse_tcp_header()` - Parse TCP header and flags
- `parse_udp_header()` - Parse UDP header
- `validate_ipv4_checksum()` - Verify packet integrity

### Logger (`src/logger.c`)
Provides formatted, colored logging with multiple severity levels.

**Features:**
- Multiple log levels (DEBUG, INFO, WARN, ERROR, CRITICAL)
- Colored console output
- File logging support
- Hex dump utility for binary data

### Circular Buffer (`src/buffer.c`)
Memory-efficient buffer for handling continuous packet flow.

**Key Functions:**
- `buffer_create()` - Allocate buffer of given size
- `buffer_write()` - Add data to buffer
- `buffer_read()` - Extract data from buffer
- `buffer_get_available()` - Check available space

## Building and Running

### Standard Build
```bash
make
```

### Debug Build (with debug symbols)
```bash
make debug
```

### Clean Build Artifacts
```bash
make clean
```

### Run with All Debugging
```bash
sudo ./build/packet_analyzer -i en0 -d -n 50
```

## Command-Line Options

| Option | Description | Example |
|--------|-------------|---------|
| `-i INTERFACE` | Network interface to monitor | `-i en0` (macOS) or `-i eth0` (Linux) |
| `-n COUNT` | Limit packets to capture | `-n 100` |
| `-t THREADS` | Number of processing threads | `-t 4` |
| `-d` | Enable debug logging | `-d` |
| `-h` | Show help message | `-h` |

## Example Usage Scenarios

### Capture 100 packets with debug output
```bash
sudo ./build/packet_analyzer -i eth0 -n 100 -d
```

### Monitor all interfaces with 8 threads
```bash
sudo ./build/packet_analyzer -t 8
```

### Continuous monitoring (no packet limit)
```bash
sudo ./build/packet_analyzer -i eth0 &
# ... monitor for a while ...
# Press Ctrl+C to stop
```

## Testing Without Root

The test program demonstrates core functionality without needing packet capture:

```bash
make
./build/test_basic
```

This tests:
- Logger system with colored output
- Circular buffer operations
- Packet parsing
- Hex dump generation

## Performance Tips

1. **Thread Count**: Use 4-8 threads for modern CPUs (match logical core count)
2. **Queue Size**: Default is 100, increase for burst traffic
3. **Debug Logging**: Disable with `-d` flag removed for production use
4. **Network Interface**: Avoid monitoring high-traffic interfaces without filtering

## Extending the Analyzer

### Adding a New Protocol

1. Define protocol header in `include/parser.h`:
```c
typedef struct {
    uint16_t field1;
    uint8_t field2;
    // ... more fields
} my_protocol_t;
```

2. Implement parsing in `src/parser.c`:
```c
void parse_my_protocol(packet_t *packet, const uint8_t *data) {
    packet->my_protocol = (my_protocol_t *)malloc(sizeof(my_protocol_t));
    memcpy(packet->my_protocol, data, sizeof(my_protocol_t));
}
```

3. Integrate into main parsing flow in `src/packet.c`

## Troubleshooting

### "Permission Denied"
```bash
sudo ./build/packet_analyzer -i en0
```

### "No such device"
Check available interfaces:
```bash
ifconfig          # macOS
# or
ip link show      # Linux
```

### Build Failures
Ensure development tools are installed:
```bash
# macOS
xcode-select --install

# Linux (Ubuntu/Debian)
sudo apt-get install build-essential
```

## Next Steps

1. **Understand the Code**: Start with `src/main.c` and follow the flow
2. **Read Headers**: Review `include/*.h` for API documentation
3. **Modify for Your Needs**: Add filtering, statistics, or new protocols
4. **Optimize**: Profile with `gprof` or `perf` on Linux
5. **Extend**: Add IPv6, ICMP, or other protocols

## Additional Resources

- [POSIX Sockets](https://man7.org/linux/man-pages/man7/socket.7.html)
- [TCP/IP Protocol Stack](https://en.wikipedia.org/wiki/Internet_protocol_suite)
- [Berkeley Packet Filter](https://www.kernel.org/doc/html/latest/userspace-api/eBPF/index.html)
- [Pthreads Documentation](https://man7.org/linux/man-pages/man7/pthreads.7.html)

## Support

For issues:
1. Enable debug logging: `-d` flag
2. Check system interface availability: `ifconfig` or `ip link show`
3. Verify you have root/sudo access
4. Review the README.md for detailed documentation

---

**Happy packet analyzing!** 📦🔍
