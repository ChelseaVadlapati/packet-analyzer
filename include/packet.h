#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <time.h>

/* IPv4 Header Structure */
typedef struct {
    uint8_t version_ihl;        /* Version and Internet Header Length */
    uint8_t dscp_ecn;           /* Differentiated Services Code Point and ECN */
    uint16_t total_length;      /* Total length of the packet */
    uint16_t identification;    /* Identification */
    uint16_t flags_fragment;    /* Flags and Fragment Offset */
    uint8_t ttl;                /* Time to Live */
    uint8_t protocol;           /* Protocol (TCP=6, UDP=17, etc) */
    uint16_t checksum;          /* Header Checksum */
    uint32_t src_ip;            /* Source IP Address */
    uint32_t dst_ip;            /* Destination IP Address */
} ipv4_header_t;

/* TCP Header Structure */
typedef struct {
    uint16_t src_port;          /* Source Port */
    uint16_t dst_port;          /* Destination Port */
    uint32_t seq_num;           /* Sequence Number */
    uint32_t ack_num;           /* Acknowledgment Number */
    uint8_t data_offset;        /* Data Offset and Reserved */
    uint8_t flags;              /* TCP Flags (SYN, ACK, FIN, etc) */
    uint16_t window_size;       /* Window Size */
    uint16_t checksum;          /* Checksum */
    uint16_t urgent_ptr;        /* Urgent Pointer */
} tcp_header_t;

/* UDP Header Structure */
typedef struct {
    uint16_t src_port;          /* Source Port */
    uint16_t dst_port;          /* Destination Port */
    uint16_t length;            /* Length */
    uint16_t checksum;          /* Checksum */
} udp_header_t;

/* Ethernet Frame Structure */
typedef struct {
    uint8_t dst_mac[6];         /* Destination MAC Address */
    uint8_t src_mac[6];         /* Source MAC Address */
    uint16_t ethertype;         /* EtherType (0x0800 for IPv4) */
} ethernet_header_t;

/* Captured Packet Structure */
typedef struct {
    time_t timestamp;           /* Packet capture timestamp (wall clock) */
    uint64_t capture_ts_ns;     /* High-resolution capture timestamp (CLOCK_MONOTONIC, ns) */
    uint32_t packet_length;     /* Total packet length */
    uint8_t *raw_data;          /* Raw packet data */
    
    /* Parsed Headers */
    ethernet_header_t *ethernet;
    ipv4_header_t *ipv4;
    tcp_header_t *tcp;
    udp_header_t *udp;
    
    /* Payload */
    uint8_t *payload;
    uint32_t payload_length;
} packet_t;

/* Function Declarations */
packet_t* packet_create(uint8_t *raw_data, uint32_t length);
void packet_free(packet_t *packet);
void packet_parse(packet_t *packet);
void packet_print(packet_t *packet);

#endif /* PACKET_H */
