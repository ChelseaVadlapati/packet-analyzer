#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "packet.h"
#include "logger.h"

packet_t* packet_create(uint8_t *raw_data, uint32_t length) {
    if (raw_data == NULL || length == 0) {
        logger_error("Invalid packet data: raw_data=%p, length=%u", raw_data, length);
        return NULL;
    }

    packet_t *packet = (packet_t *)malloc(sizeof(packet_t));
    if (packet == NULL) {
        logger_error("Failed to allocate memory for packet structure");
        return NULL;
    }

    packet->raw_data = (uint8_t *)malloc(length);
    if (packet->raw_data == NULL) {
        logger_error("Failed to allocate memory for packet raw data");
        free(packet);
        return NULL;
    }

    memcpy(packet->raw_data, raw_data, length);
    packet->packet_length = length;
    packet->timestamp = time(NULL);
    
    packet->ethernet = NULL;
    packet->ipv4 = NULL;
    packet->tcp = NULL;
    packet->udp = NULL;
    packet->payload = NULL;
    packet->payload_length = 0;

    return packet;
}

void packet_free(packet_t *packet) {
    if (packet == NULL) return;

    if (packet->raw_data != NULL) free(packet->raw_data);
    if (packet->ethernet != NULL) free(packet->ethernet);
    if (packet->ipv4 != NULL) free(packet->ipv4);
    if (packet->tcp != NULL) free(packet->tcp);
    if (packet->udp != NULL) free(packet->udp);
    if (packet->payload != NULL) free(packet->payload);
    
    free(packet);
}

void packet_parse(packet_t *packet) {
    if (packet == NULL || packet->raw_data == NULL) {
        logger_error("Invalid packet for parsing");
        return;
    }

    uint32_t offset = 0;

    /* Parse Ethernet header (14 bytes minimum) */
    if (packet->packet_length >= sizeof(ethernet_header_t)) {
        packet->ethernet = (ethernet_header_t *)malloc(sizeof(ethernet_header_t));
        if (packet->ethernet != NULL) {
            memcpy(packet->ethernet, packet->raw_data + offset, sizeof(ethernet_header_t));
            offset += sizeof(ethernet_header_t);
            logger_debug("Parsed Ethernet header");
        }
    } else {
        logger_warn("Packet too small for Ethernet header");
        return;
    }

    /* Check for IPv4 (0x0800) */
    if (packet->ethernet != NULL && ntohs(packet->ethernet->ethertype) == 0x0800) {
        /* Parse IPv4 header (minimum 20 bytes) */
        if (packet->packet_length - offset >= sizeof(ipv4_header_t)) {
            packet->ipv4 = (ipv4_header_t *)malloc(sizeof(ipv4_header_t));
            if (packet->ipv4 != NULL) {
                memcpy(packet->ipv4, packet->raw_data + offset, sizeof(ipv4_header_t));
                uint8_t ihl = (packet->ipv4->version_ihl & 0x0F) * 4;
                offset += ihl;
                logger_debug("Parsed IPv4 header (IHL=%u)", ihl);
                
                /* Parse TCP header (minimum 20 bytes) */
                if (packet->ipv4->protocol == 6 && packet->packet_length - offset >= sizeof(tcp_header_t)) {
                    packet->tcp = (tcp_header_t *)malloc(sizeof(tcp_header_t));
                    if (packet->tcp != NULL) {
                        memcpy(packet->tcp, packet->raw_data + offset, sizeof(tcp_header_t));
                        uint8_t data_offset = (packet->tcp->data_offset >> 4) * 4;
                        offset += data_offset;
                        logger_debug("Parsed TCP header (Offset=%u)", data_offset);
                    }
                }
                /* Parse UDP header (8 bytes) */
                else if (packet->ipv4->protocol == 17 && packet->packet_length - offset >= sizeof(udp_header_t)) {
                    packet->udp = (udp_header_t *)malloc(sizeof(udp_header_t));
                    if (packet->udp != NULL) {
                        memcpy(packet->udp, packet->raw_data + offset, sizeof(udp_header_t));
                        offset += sizeof(udp_header_t);
                        logger_debug("Parsed UDP header");
                    }
                }
            }
        } else {
            logger_warn("Packet too small for IPv4 header");
        }
    }

    /* Extract payload */
    if (offset < packet->packet_length) {
        packet->payload_length = packet->packet_length - offset;
        packet->payload = (uint8_t *)malloc(packet->payload_length);
        if (packet->payload != NULL) {
            memcpy(packet->payload, packet->raw_data + offset, packet->payload_length);
            logger_debug("Extracted payload (%u bytes)", packet->payload_length);
        }
    }
}

void packet_print(packet_t *packet) {
    if (packet == NULL) {
        logger_error("Cannot print NULL packet");
        return;
    }

    logger_info("=== Packet Information ===");
    logger_info("Timestamp: %ld", packet->timestamp);
    logger_info("Total Length: %u bytes", packet->packet_length);

    if (packet->ethernet != NULL) {
        logger_info("Ethernet: %02x:%02x:%02x:%02x:%02x:%02x -> %02x:%02x:%02x:%02x:%02x:%02x",
                    packet->ethernet->src_mac[0], packet->ethernet->src_mac[1], packet->ethernet->src_mac[2],
                    packet->ethernet->src_mac[3], packet->ethernet->src_mac[4], packet->ethernet->src_mac[5],
                    packet->ethernet->dst_mac[0], packet->ethernet->dst_mac[1], packet->ethernet->dst_mac[2],
                    packet->ethernet->dst_mac[3], packet->ethernet->dst_mac[4], packet->ethernet->dst_mac[5]);
    }

    if (packet->ipv4 != NULL) {
        struct in_addr src, dst;
        src.s_addr = packet->ipv4->src_ip;
        dst.s_addr = packet->ipv4->dst_ip;
        logger_info("IPv4: %s -> %s (TTL=%u, Protocol=%u)", 
                    inet_ntoa(src), inet_ntoa(dst), 
                    packet->ipv4->ttl, packet->ipv4->protocol);
    }

    if (packet->tcp != NULL) {
        logger_info("TCP: Port %u -> %u (Seq=%u, Ack=%u, Flags=0x%02x)",
                    ntohs(packet->tcp->src_port), ntohs(packet->tcp->dst_port),
                    ntohl(packet->tcp->seq_num), ntohl(packet->tcp->ack_num),
                    packet->tcp->flags);
    }

    if (packet->udp != NULL) {
        logger_info("UDP: Port %u -> %u (Length=%u)",
                    ntohs(packet->udp->src_port), ntohs(packet->udp->dst_port),
                    ntohs(packet->udp->length));
    }

    if (packet->payload_length > 0) {
        logger_hexdump("Payload", packet->payload, 
                      (packet->payload_length > 64 ? 64 : packet->payload_length));
    }
}
