#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "packet.h"
#include "buffer.h"
#include "logger.h"

void test_logger() {
    printf("\n=== Testing Logger ===\n");
    logger_init(NULL, LOG_DEBUG);
    
    logger_debug("This is a debug message");
    logger_info("This is an info message");
    logger_warn("This is a warning message");
    logger_error("This is an error message");
    
    uint8_t test_data[] = {0x45, 0x00, 0x00, 0x34, 0x1c, 0x46, 0x40, 0x00, 
                           0x40, 0x06, 0x4c, 0xe7, 0xac, 0x10, 0x0a, 0x63};
    logger_hexdump("Sample IPv4 Header", test_data, sizeof(test_data));
    
    logger_cleanup();
}

void test_buffer() {
    printf("\n=== Testing Circular Buffer ===\n");
    logger_init(NULL, LOG_INFO);
    
    circular_buffer_t *buffer = buffer_create(256);
    
    uint8_t write_data[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    buffer_write(buffer, write_data, 10);
    printf("Wrote 10 bytes to buffer\n");
    
    uint8_t read_data[10];
    buffer_read(buffer, read_data, 10);
    printf("Read 10 bytes from buffer: ");
    for (int i = 0; i < 10; i++) {
        printf("%d ", read_data[i]);
    }
    printf("\n");
    
    printf("Available bytes in buffer: %zu\n", buffer_get_available(buffer));
    
    buffer_free(buffer);
    logger_cleanup();
}

void test_packet() {
    printf("\n=== Testing Packet Structure ===\n");
    logger_init(NULL, LOG_DEBUG);
    
    /* Create a simple Ethernet + IPv4 + TCP packet */
    uint8_t test_packet[60] = {
        /* Ethernet header (14 bytes) */
        0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e,  /* Dest MAC */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,  /* Src MAC */
        0x08, 0x00,                           /* EtherType (IPv4) */
        
        /* IPv4 header (20 bytes) */
        0x45, 0x00, 0x00, 0x3c,               /* Version, IHL, DSCP, ECN, Total Length */
        0x1c, 0x46, 0x40, 0x00,               /* Identification, Flags, Fragment Offset */
        0x40, 0x06, 0x4c, 0xe7,               /* TTL, Protocol (TCP), Checksum */
        0xac, 0x10, 0x0a, 0x63,               /* Source IP: 172.16.10.99 */
        0xac, 0x10, 0x0a, 0x01,               /* Dest IP: 172.16.10.1 */
        
        /* TCP header (20 bytes) */
        0x00, 0x50, 0x12, 0x34,               /* Src Port (80), Dst Port (4660) */
        0x00, 0x00, 0x00, 0x01,               /* Sequence Number */
        0x00, 0x00, 0x00, 0x00,               /* Acknowledgment Number */
        0x50, 0x02, 0x20, 0x00,               /* Data Offset, Flags, Window Size */
        0x00, 0x00, 0x00, 0x00,               /* Checksum, Urgent Pointer */
    };
    
    packet_t *packet = packet_create(test_packet, sizeof(test_packet));
    if (packet != NULL) {
        packet_parse(packet);
        packet_print(packet);
        packet_free(packet);
    }
    
    logger_cleanup();
}

int main() {
    printf("=== Network Packet Analyzer - Unit Tests ===\n");
    
    test_logger();
    test_buffer();
    test_packet();
    
    printf("\n=== All Tests Completed ===\n");
    return 0;
}
