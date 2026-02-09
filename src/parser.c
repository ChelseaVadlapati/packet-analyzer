#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include "parser.h"
#include "logger.h"

static parser_stats_t global_stats = {0};

void parse_ethernet_header(packet_t *packet, const uint8_t *data) {
    (void)data;  /* Unused parameter */
    if (packet == NULL || packet->ethernet == NULL) {
        return;
    }

    logger_debug("Ethernet frame: %02x:%02x:%02x:%02x:%02x:%02x -> %02x:%02x:%02x:%02x:%02x:%02x (Type: 0x%04x)",
                 packet->ethernet->src_mac[0], packet->ethernet->src_mac[1], packet->ethernet->src_mac[2],
                 packet->ethernet->src_mac[3], packet->ethernet->src_mac[4], packet->ethernet->src_mac[5],
                 packet->ethernet->dst_mac[0], packet->ethernet->dst_mac[1], packet->ethernet->dst_mac[2],
                 packet->ethernet->dst_mac[3], packet->ethernet->dst_mac[4], packet->ethernet->dst_mac[5],
                 ntohs(packet->ethernet->ethertype));
}

void parse_ipv4_header(packet_t *packet, const uint8_t *data) {
    (void)data;  /* Unused parameter */
    if (packet == NULL || packet->ipv4 == NULL) {
        return;
    }

    global_stats.ipv4_packets++;

    uint8_t version = (packet->ipv4->version_ihl >> 4) & 0x0F;
    uint8_t ihl = (packet->ipv4->version_ihl & 0x0F) * 4;
    uint16_t total_length = ntohs(packet->ipv4->total_length);
    uint8_t ttl = packet->ipv4->ttl;
    uint8_t protocol = packet->ipv4->protocol;

    struct in_addr src, dst;
    src.s_addr = packet->ipv4->src_ip;
    dst.s_addr = packet->ipv4->dst_ip;

    logger_debug("IPv4 Header: %s -> %s", inet_ntoa(src), inet_ntoa(dst));
    logger_debug("  Version: %u, IHL: %u bytes, Total Length: %u bytes", version, ihl, total_length);
    logger_debug("  TTL: %u, Protocol: %u, Checksum: 0x%04x", ttl, protocol, ntohs(packet->ipv4->checksum));

    if (!validate_ipv4_checksum(packet->ipv4)) {
        logger_warn("IPv4 checksum validation failed");
        global_stats.malformed_packets++;
    }
}

void parse_tcp_header(packet_t *packet, const uint8_t *data) {
    (void)data;  /* Unused parameter */
    if (packet == NULL || packet->tcp == NULL) {
        return;
    }

    global_stats.tcp_packets++;

    uint16_t src_port = ntohs(packet->tcp->src_port);
    uint16_t dst_port = ntohs(packet->tcp->dst_port);
    uint32_t seq = ntohl(packet->tcp->seq_num);
    uint32_t ack = ntohl(packet->tcp->ack_num);
    uint8_t flags = packet->tcp->flags;
    uint16_t window = ntohs(packet->tcp->window_size);

    logger_debug("TCP Header: %u -> %u", src_port, dst_port);
    logger_debug("  Seq: %u, Ack: %u, Window: %u", seq, ack, window);
    logger_debug("  Flags: [%s%s%s%s%s%s]",
                 (flags & 0x01) ? "FIN " : "",
                 (flags & 0x02) ? "SYN " : "",
                 (flags & 0x04) ? "RST " : "",
                 (flags & 0x08) ? "PSH " : "",
                 (flags & 0x10) ? "ACK " : "",
                 (flags & 0x20) ? "URG " : "");
}

void parse_udp_header(packet_t *packet, const uint8_t *data) {
    (void)data;  /* Unused parameter */
    if (packet == NULL || packet->udp == NULL) {
        return;
    }

    global_stats.udp_packets++;

    uint16_t src_port = ntohs(packet->udp->src_port);
    uint16_t dst_port = ntohs(packet->udp->dst_port);
    uint16_t length = ntohs(packet->udp->length);

    logger_debug("UDP Header: %u -> %u", src_port, dst_port);
    logger_debug("  Length: %u, Checksum: 0x%04x", length, ntohs(packet->udp->checksum));
}

int validate_ipv4_checksum(const ipv4_header_t *header) {
    if (header == NULL) return 0;

    uint32_t sum = 0;
    const uint16_t *data = (const uint16_t *)header;
    int ihl = (header->version_ihl & 0x0F) * 2; /* Convert bytes to 16-bit words */

    for (int i = 0; i < ihl; i++) {
        if (i != 5) { /* Skip checksum field */
            sum += ntohs(data[i]);
        }
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ((uint16_t)~sum == ntohs(header->checksum));
}

int validate_tcp_checksum(const ipv4_header_t *ipv4, const tcp_header_t *tcp, const uint8_t *data, uint32_t len) {
    (void)ipv4;  /* Unused parameter */
    (void)data;  /* Unused parameter */
    (void)len;   /* Unused parameter */
    
    if (tcp == NULL) return 0;
    
    /* Simplified validation - full implementation would require pseudo-header calculation */
    logger_debug("TCP checksum validation: 0x%04x", ntohs(tcp->checksum));
    return 1;
}

void print_packet_info(packet_t *packet) {
    if (packet == NULL) {
        logger_error("Cannot print NULL packet");
        return;
    }

    global_stats.total_packets++;
    global_stats.total_bytes += packet->packet_length;

    packet_print(packet);
}

void print_statistics(parser_stats_t *stats) {
    if (stats == NULL) {
        stats = &global_stats;
    }

    logger_info("===== Packet Statistics =====");
    logger_info("Total packets: %u", stats->total_packets);
    logger_info("IPv4 packets: %u", stats->ipv4_packets);
    logger_info("TCP packets: %u", stats->tcp_packets);
    logger_info("UDP packets: %u", stats->udp_packets);
    logger_info("Malformed packets: %u", stats->malformed_packets);
    logger_info("Total bytes: %llu", stats->total_bytes);

    if (stats->total_packets > 0) {
        double avg_size = (double)stats->total_bytes / stats->total_packets;
        logger_info("Average packet size: %.2f bytes", avg_size);
    }
}
