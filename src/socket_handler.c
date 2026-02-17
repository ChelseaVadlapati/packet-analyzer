#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <errno.h>
#include "socket_handler.h"
#include "logger.h"

/* Platform-specific includes */
#ifdef __linux__
    #include <linux/if_packet.h>
    #include <net/ethernet.h>
#elif __APPLE__
    #include <fcntl.h>
    #include <net/bpf.h>
    #include <sys/uio.h>
#endif

socket_config_t* socket_init(const char *interface_name) {
    socket_config_t *config = (socket_config_t *)malloc(sizeof(socket_config_t));
    if (config == NULL) {
        logger_error("Failed to allocate memory for socket config");
        return NULL;
    }

    memset(config, 0, sizeof(socket_config_t));

    config->interface_name = (char *)malloc(strlen(interface_name) + 1);
    if (config->interface_name == NULL) {
        logger_error("Failed to allocate memory for interface name");
        free(config);
        return NULL;
    }

    strcpy(config->interface_name, interface_name);
    config->socket_fd = -1;
    config->promiscuous_mode = 0;
    config->filter = FILTER_NONE;
    
    /* Initialize BPF state */
    config->bpf_buffer = NULL;
    config->bpf_buffer_size = 0;
    config->bpf_data_len = 0;
    config->bpf_offset = 0;

    logger_info("Socket configuration initialized for interface: %s", interface_name);
    return config;
}

int socket_bind_raw(socket_config_t *config) {
    if (config == NULL || config->interface_name == NULL) {
        logger_error("Invalid socket configuration");
        return -1;
    }

#ifdef __linux__
    /* Create raw socket for Linux */
    config->socket_fd = socket(AF_PACKET, SOCK_RAW, htons(0x0003));
    if (config->socket_fd < 0) {
        logger_error("Failed to create raw socket (requires root/sudo)");
        return -1;
    }
    logger_info("Raw socket created (fd: %d) on Linux", config->socket_fd);
#elif __APPLE__
    /* Use BPF device for macOS */
    char bpf_path[256];
    int i;
    for (i = 0; i < 256; i++) {
        snprintf(bpf_path, sizeof(bpf_path), "/dev/bpf%d", i);
        config->socket_fd = open(bpf_path, O_RDONLY);
        if (config->socket_fd >= 0) {
            logger_info("BPF device opened: %s (fd: %d)", bpf_path, config->socket_fd);
            break;
        }
    }
    if (config->socket_fd < 0) {
        logger_error("Failed to open BPF device (run with sudo)");
        return -1;
    }
    
    /* Get BPF buffer length requirement */
    unsigned int bpf_len = 0;
    if (ioctl(config->socket_fd, BIOCGBLEN, &bpf_len) < 0) {
        logger_warn("Failed to get BPF buffer length, using default");
        bpf_len = BPF_BUFFER_SIZE;
    }
    
    /* Try to set larger buffer size */
    unsigned int desired_len = BPF_BUFFER_SIZE;
    if (ioctl(config->socket_fd, BIOCSBLEN, &desired_len) == 0) {
        bpf_len = desired_len;
        logger_info("BPF buffer size set to %u bytes", bpf_len);
    } else {
        logger_info("Using default BPF buffer size: %u bytes", bpf_len);
    }
    
    /* Allocate BPF buffer */
    config->bpf_buffer = (uint8_t *)malloc(bpf_len);
    if (config->bpf_buffer == NULL) {
        logger_error("Failed to allocate BPF buffer");
        close(config->socket_fd);
        config->socket_fd = -1;
        return -1;
    }
    config->bpf_buffer_size = bpf_len;
    config->bpf_data_len = 0;
    config->bpf_offset = 0;
#else
    logger_error("Unsupported platform for raw socket creation");
    return -1;
#endif
    
    return 0;
}

int socket_enable_promiscuous(socket_config_t *config) {
    if (config == NULL || config->socket_fd < 0) {
        logger_error("Invalid socket configuration");
        return -1;
    }

    /* Get interface index */
    unsigned int if_index = if_nametoindex(config->interface_name);
    if (if_index == 0) {
        logger_error("Failed to get interface index for: %s", config->interface_name);
        return -1;
    }

#ifdef __linux__
    /* Linux: sockaddr_ll structure */
    struct {
        unsigned short sll_family;
        unsigned short sll_protocol;
        int sll_ifindex;
        unsigned short sll_hatype;
        unsigned char sll_pkttype;
        unsigned char sll_halen;
        unsigned char sll_addr[8];
    } sll = {0};
    
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = if_index;
    sll.sll_protocol = htons(0x0003);

    if (bind(config->socket_fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        logger_error("Failed to bind socket to interface");
        return -1;
    }
#elif __APPLE__
    /* macOS: BPF interface binding with BIOCSETIF */
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, config->interface_name, sizeof(ifr.ifr_name) - 1);
    
    if (ioctl(config->socket_fd, BIOCSETIF, &ifr) < 0) {
        logger_error("Failed to bind BPF to interface %s: %s", 
                     config->interface_name, strerror(errno));
        close(config->socket_fd);
        config->socket_fd = -1;
        return -1;
    }
    logger_info("BPF bound to interface: %s", config->interface_name);
    
    /* Enable immediate mode - don't wait for buffer to fill */
    unsigned int immediate = 1;
    if (ioctl(config->socket_fd, BIOCIMMEDIATE, &immediate) < 0) {
        logger_error("Failed to enable BPF immediate mode: %s", strerror(errno));
        close(config->socket_fd);
        config->socket_fd = -1;
        return -1;
    }
    logger_info("BPF immediate mode enabled");
    
    /* Enable promiscuous mode on interface */
    unsigned int promisc = 1;
    if (ioctl(config->socket_fd, BIOCPROMISC, &promisc) < 0) {
        logger_warn("Failed to enable BPF promiscuous mode: %s (continuing anyway)", 
                    strerror(errno));
        /* Don't fail - some interfaces don't support promiscuous mode */
    } else {
        logger_info("BPF promiscuous mode enabled");
    }
    
    /* See sent packets too */
    unsigned int see_sent = 1;
    if (ioctl(config->socket_fd, BIOCSSEESENT, &see_sent) < 0) {
        logger_debug("BIOCSSEESENT not supported (continuing)");
    }
#endif

    config->promiscuous_mode = 1;
    logger_info("Packet capture enabled on interface: %s", config->interface_name);
    return 0;
}

int socket_receive_packet(socket_config_t *config, uint8_t *buffer, size_t buffer_size) {
    if (config == NULL || config->socket_fd < 0 || buffer == NULL || buffer_size == 0) {
        logger_error("Invalid parameters for packet reception");
        return -1;
    }

#ifdef __linux__
    struct {
        unsigned short sll_family;
        unsigned short sll_protocol;
        int sll_ifindex;
        unsigned short sll_hatype;
        unsigned char sll_pkttype;
        unsigned char sll_halen;
        unsigned char sll_addr[8];
    } sll;
    socklen_t sll_len = sizeof(sll);

    ssize_t packet_size = recvfrom(config->socket_fd, buffer, buffer_size, 0,
                                   (struct sockaddr *)&sll, &sll_len);

    if (packet_size < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;  /* No packet available, not an error */
        }
        logger_error("Failed to receive packet: %s", strerror(errno));
        return -1;
    }

    logger_debug("Received packet: %zd bytes on interface %u", packet_size, sll.sll_ifindex);
    return (int)packet_size;
    
#elif __APPLE__
    /* 
     * macOS BPF packet handling:
     * - BPF returns multiple packets in a single read()
     * - Each packet has a bpf_hdr prefix
     * - Must use BPF_WORDALIGN to find next packet
     */
    
    /* Check if we have more packets in the current buffer */
    while (config->bpf_offset < config->bpf_data_len) {
        /* Parse BPF header at current offset */
        struct bpf_hdr *bh = (struct bpf_hdr *)(config->bpf_buffer + config->bpf_offset);
        
        /* Validate header */
        if (bh->bh_caplen == 0 || bh->bh_hdrlen == 0) {
            /* Invalid header, reset buffer */
            config->bpf_data_len = 0;
            config->bpf_offset = 0;
            break;
        }
        
        /* Get pointer to actual packet data (after BPF header) */
        uint8_t *pkt_data = config->bpf_buffer + config->bpf_offset + bh->bh_hdrlen;
        uint32_t pkt_len = bh->bh_caplen;
        
        /* Advance offset to next packet (word-aligned) */
        config->bpf_offset += BPF_WORDALIGN(bh->bh_hdrlen + bh->bh_caplen);
        
        /* Copy packet to caller's buffer */
        if (pkt_len > buffer_size) {
            logger_warn("Packet truncated: %u bytes > buffer %zu bytes", pkt_len, buffer_size);
            pkt_len = (uint32_t)buffer_size;
        }
        
        memcpy(buffer, pkt_data, pkt_len);
        logger_debug("BPF packet: %u bytes (captured), %u bytes (wire)", 
                     bh->bh_caplen, bh->bh_datalen);
        
        return (int)pkt_len;
    }
    
    /* No more packets in buffer, need to read more from BPF device */
    config->bpf_offset = 0;
    config->bpf_data_len = 0;
    
    ssize_t bytes_read = read(config->socket_fd, config->bpf_buffer, config->bpf_buffer_size);
    
    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            /* No data available - this is normal for non-blocking or idle periods */
            return 0;
        }
        logger_error("BPF read failed: %s", strerror(errno));
        return -1;
    }
    
    if (bytes_read == 0) {
        /* No data available */
        return 0;
    }
    
    config->bpf_data_len = (size_t)bytes_read;
    logger_debug("BPF read: %zd bytes", bytes_read);
    
    /* Recursively call to parse first packet from new buffer */
    return socket_receive_packet(config, buffer, buffer_size);
    
#else
    logger_error("Unsupported platform");
    return -1;
#endif
}

int socket_set_filter(socket_config_t *config, socket_filter_t filter) {
    if (config == NULL || config->socket_fd < 0) {
        logger_error("Invalid socket configuration for filter");
        return -1;
    }
    
    if (filter == FILTER_NONE) {
        config->filter = FILTER_NONE;
        logger_info("No packet filter applied (capturing all traffic)");
        return 0;
    }
    
    if (filter == FILTER_ICMP) {
        /*
         * BPF filter for ICMP (IPv4) and ICMPv6:
         * 
         * Ethernet frame layout:
         *   [0-5]   Destination MAC
         *   [6-11]  Source MAC
         *   [12-13] EtherType (0x0800=IPv4, 0x86DD=IPv6)
         *   [14+]   Payload (IP header)
         * 
         * IPv4 header:
         *   [14]    Version/IHL
         *   [23]    Protocol (1=ICMP)
         * 
         * IPv6 header:
         *   [14-17] Version/Traffic Class/Flow Label
         *   [20]    Next Header (58=ICMPv6)
         * 
         * Filter logic:
         *   (EtherType == 0x0800 && IP.Protocol == 1) ||
         *   (EtherType == 0x86DD && IPv6.NextHeader == 58)
         */
        
#ifdef __linux__
        /* Linux: Use SO_ATTACH_FILTER with classic BPF */
        #include <linux/filter.h>
        
        /*
         * BPF filter program for ICMP/ICMPv6:
         * 
         * 0: ldh [12]              ; Load EtherType
         * 1: jeq 0x0800, 2, 5      ; If IPv4, goto 2; else goto 5
         * 2: ldb [23]              ; Load IP protocol
         * 3: jeq 1, 8, 9           ; If ICMP (1), goto accept; else reject
         * 4: jeq 0x86DD, 6, 9      ; (unreachable - kept for alignment)
         * 5: jeq 0x86DD, 6, 9      ; If IPv6, goto 6; else reject
         * 6: ldb [20]              ; Load IPv6 next header
         * 7: jeq 58, 8, 9          ; If ICMPv6 (58), goto accept; else reject
         * 8: ret 65535             ; Accept packet
         * 9: ret 0                 ; Reject packet
         */
        struct sock_filter icmp_filter[] = {
            /* 0: Load EtherType at offset 12 */
            BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
            /* 1: If IPv4 (0x0800), continue to next; else jump +4 to IPv6 check */
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x0800, 0, 4),
            /* 2: Load IP protocol at offset 23 */
            BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 23),
            /* 3: If ICMP (1), jump +4 to accept; else jump +5 to reject */
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 1, 4, 5),
            /* 4: (Skipped via jump) */
            BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
            /* 5: If IPv6 (0x86DD), continue; else jump +3 to reject */
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x86DD, 0, 3),
            /* 6: Load IPv6 next header at offset 20 */
            BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 20),
            /* 7: If ICMPv6 (58), jump +1 to accept; else jump +2 to reject */
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 58, 0, 1),
            /* 8: Accept - return max length */
            BPF_STMT(BPF_RET | BPF_K, 65535),
            /* 9: Reject - return 0 */
            BPF_STMT(BPF_RET | BPF_K, 0),
        };
        
        struct sock_fprog prog = {
            .len = sizeof(icmp_filter) / sizeof(icmp_filter[0]),
            .filter = icmp_filter
        };
        
        if (setsockopt(config->socket_fd, SOL_SOCKET, SO_ATTACH_FILTER, 
                       &prog, sizeof(prog)) < 0) {
            logger_error("Failed to attach ICMP filter (SO_ATTACH_FILTER): %s", 
                         strerror(errno));
            return -1;
        }
        
        config->filter = FILTER_ICMP;
        logger_info("ICMP filter attached via SO_ATTACH_FILTER (IPv4 ICMP + IPv6 ICMPv6)");
        return 0;
        
#elif __APPLE__
        /* macOS: Use BIOCSETFNR (or BIOCSETF) with BPF program */
        
        /*
         * BPF filter program for ICMP/ICMPv6 (10 instructions):
         * 
         * 0: ldh [12]              ; Load EtherType (2 bytes at offset 12)
         * 1: jeq 0x0800, 0, 4      ; If IPv4, continue; else jump +4 to IPv6 check
         * 2: ldb [23]              ; Load IP protocol byte
         * 3: jeq 1, 4, 5           ; If ICMP, jump to accept; else reject
         * 4: ldh [12]              ; (Re-load EtherType for IPv6 path)
         * 5: jeq 0x86DD, 0, 3      ; If IPv6, continue; else jump to reject
         * 6: ldb [20]              ; Load IPv6 next header
         * 7: jeq 58, 0, 1          ; If ICMPv6, continue to accept; else reject
         * 8: ret -1                ; Accept packet (capture full length)
         * 9: ret 0                 ; Reject packet
         */
        struct bpf_insn icmp_filter[] = {
            /* 0: Load EtherType at offset 12 */
            BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
            /* 1: If IPv4 (0x0800), continue; else jump +4 to instruction 5 */
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x0800, 0, 4),
            /* 2: Load IP protocol at offset 23 */
            BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 23),
            /* 3: If ICMP (1), jump +4 to accept; else jump +5 to reject */
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 1, 4, 5),
            /* 4: Re-load EtherType (entry point for non-IPv4) */
            BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
            /* 5: If IPv6 (0x86DD), continue; else jump +3 to reject */
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x86DD, 0, 3),
            /* 6: Load IPv6 next header at offset 20 */
            BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 20),
            /* 7: If ICMPv6 (58), continue to accept; else jump +1 to reject */
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 58, 0, 1),
            /* 8: Accept - return max capture length */
            BPF_STMT(BPF_RET | BPF_K, (u_int)-1),
            /* 9: Reject - return 0 */
            BPF_STMT(BPF_RET | BPF_K, 0),
        };
        
        struct bpf_program prog = {
            .bf_len = sizeof(icmp_filter) / sizeof(icmp_filter[0]),
            .bf_insns = icmp_filter
        };
        
        /* Try BIOCSETFNR first (non-resetting), fall back to BIOCSETF */
#ifdef BIOCSETFNR
        if (ioctl(config->socket_fd, BIOCSETFNR, &prog) < 0) {
            logger_debug("BIOCSETFNR failed, trying BIOCSETF: %s", strerror(errno));
#endif
            if (ioctl(config->socket_fd, BIOCSETF, &prog) < 0) {
                logger_error("Failed to attach ICMP filter (BIOCSETF): %s", 
                             strerror(errno));
                return -1;
            }
#ifdef BIOCSETFNR
        }
#endif
        
        config->filter = FILTER_ICMP;
        logger_info("ICMP filter attached via BPF (IPv4 ICMP + IPv6 ICMPv6)");
        return 0;
        
#else
        logger_error("BPF filter not supported on this platform");
        return -1;
#endif
    }
    
    logger_error("Unknown filter type: %d", filter);
    return -1;
}

void socket_cleanup(socket_config_t *config) {
    if (config == NULL) return;

    if (config->socket_fd >= 0) {
        close(config->socket_fd);
        logger_info("Socket closed (fd: %d)", config->socket_fd);
    }

    if (config->interface_name != NULL) {
        free(config->interface_name);
    }
    
    /* Free BPF buffer if allocated */
    if (config->bpf_buffer != NULL) {
        free(config->bpf_buffer);
        config->bpf_buffer = NULL;
    }

    free(config);
    logger_info("Socket configuration cleaned up");
}
