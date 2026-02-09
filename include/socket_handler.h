#ifndef SOCKET_HANDLER_H
#define SOCKET_HANDLER_H

#include <stdint.h>

/* Socket configuration */
typedef struct {
    int socket_fd;              /* Socket file descriptor */
    char *interface_name;       /* Network interface name (e.g., "eth0") */
    int promiscuous_mode;       /* Enable promiscuous mode */
} socket_config_t;

/* Function Declarations */
socket_config_t* socket_init(const char *interface_name);
int socket_bind_raw(socket_config_t *config);
int socket_enable_promiscuous(socket_config_t *config);
int socket_receive_packet(socket_config_t *config, uint8_t *buffer, size_t buffer_size);
void socket_cleanup(socket_config_t *config);

#endif /* SOCKET_HANDLER_H */
