#ifndef SOCKET_HANDLER_H
#define SOCKET_HANDLER_H

#include <stdint.h>
#include <stddef.h>

/* BPF buffer size for macOS */
#define BPF_BUFFER_SIZE (128 * 1024)

/* Filter types */
typedef enum {
    FILTER_NONE = 0,       /* Capture all traffic */
    FILTER_ICMP = 1        /* ICMP/ICMPv6 only */
} socket_filter_t;

/* Socket configuration */
typedef struct {
    int socket_fd;              /* Socket file descriptor */
    char *interface_name;       /* Network interface name (e.g., "eth0") */
    int promiscuous_mode;       /* Enable promiscuous mode */
    socket_filter_t filter;     /* Active filter type */
    
    /* BPF-specific state for macOS */
    uint8_t *bpf_buffer;        /* BPF read buffer */
    size_t bpf_buffer_size;     /* Allocated buffer size */
    size_t bpf_data_len;        /* Bytes of valid data in buffer */
    size_t bpf_offset;          /* Current parse offset in buffer */
} socket_config_t;

/* Function Declarations */
socket_config_t* socket_init(const char *interface_name);
int socket_bind_raw(socket_config_t *config);
int socket_enable_promiscuous(socket_config_t *config);
int socket_set_filter(socket_config_t *config, socket_filter_t filter);
int socket_receive_packet(socket_config_t *config, uint8_t *buffer, size_t buffer_size);
void socket_cleanup(socket_config_t *config);

#endif /* SOCKET_HANDLER_H */
