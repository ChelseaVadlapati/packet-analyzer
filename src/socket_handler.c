#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include "socket_handler.h"
#include "logger.h"

/* Platform-specific includes */
#ifdef __linux__
    #include <linux/if_packet.h>
    #include <net/ethernet.h>
#elif __APPLE__
    #include <fcntl.h>
    #include <net/bpf.h>
#endif

socket_config_t* socket_init(const char *interface_name) {
    socket_config_t *config = (socket_config_t *)malloc(sizeof(socket_config_t));
    if (config == NULL) {
        logger_error("Failed to allocate memory for socket config");
        return NULL;
    }

    config->interface_name = (char *)malloc(strlen(interface_name) + 1);
    if (config->interface_name == NULL) {
        logger_error("Failed to allocate memory for interface name");
        free(config);
        return NULL;
    }

    strcpy(config->interface_name, interface_name);
    config->socket_fd = -1;
    config->promiscuous_mode = 0;

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
            logger_info("BPF device opened: %s", bpf_path);
            break;
        }
    }
    if (config->socket_fd < 0) {
        logger_error("Failed to open BPF device (run with sudo)");
        return -1;
    }
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
    /* macOS: BPF interface binding */
    struct ifreq ifr = {0};
    strncpy(ifr.ifr_name, config->interface_name, sizeof(ifr.ifr_name) - 1);
    
    if (ioctl(config->socket_fd, BIOCSETIF, &ifr) < 0) {
        logger_error("Failed to bind BPF to interface");
        close(config->socket_fd);
        config->socket_fd = -1;
        return -1;
    }
    
    /* Enable immediate mode */
    int immediate = 1;
    if (ioctl(config->socket_fd, BIOCIMMEDIATE, &immediate) < 0) {
        logger_error("Failed to enable immediate mode");
        close(config->socket_fd);
        config->socket_fd = -1;
        return -1;
    }
#endif

    config->promiscuous_mode = 1;
    logger_info("Promiscuous mode enabled on interface: %s", config->interface_name);
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
        logger_error("Failed to receive packet");
        return -1;
    }

    logger_debug("Received packet: %zd bytes on interface %u", packet_size, sll.sll_ifindex);
#elif __APPLE__
    ssize_t packet_size = read(config->socket_fd, buffer, buffer_size);
    
    if (packet_size < 0) {
        logger_error("Failed to receive packet");
        return -1;
    }
    
    logger_debug("Received packet: %zd bytes", packet_size);
#else
    return -1;
#endif

    return (int)packet_size;
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

    free(config);
    logger_info("Socket configuration cleaned up");
}
