#ifndef PARSER_H
#define PARSER_H

#include "packet.h"
#include <stdint.h>

/* Parser Statistics */
typedef struct {
    uint32_t total_packets;
    uint32_t ipv4_packets;
    uint32_t tcp_packets;
    uint32_t udp_packets;
    uint32_t malformed_packets;
    uint64_t total_bytes;
} parser_stats_t;

/* Function Declarations */
void parse_ethernet_header(packet_t *packet, const uint8_t *data);
void parse_ipv4_header(packet_t *packet, const uint8_t *data);
void parse_tcp_header(packet_t *packet, const uint8_t *data);
void parse_udp_header(packet_t *packet, const uint8_t *data);
int validate_ipv4_checksum(const ipv4_header_t *header);
int validate_tcp_checksum(const ipv4_header_t *ipv4, const tcp_header_t *tcp, const uint8_t *data, uint32_t len);
void print_packet_info(packet_t *packet);
void print_statistics(parser_stats_t *stats);

#endif /* PARSER_H */
