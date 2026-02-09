#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "packet.h"
#include "socket_handler.h"
#include "thread_pool.h"
#include "logger.h"
#include "parser.h"

#define MAX_PACKET_SIZE 65535
#define NUM_THREADS 4
#define MAX_QUEUE_SIZE 100
#define PACKETS_TO_CAPTURE 0 /* 0 = unlimited */

static volatile int is_running = 1;
static socket_config_t *socket_config = NULL;
static thread_pool_t *thread_pool = NULL;
static uint32_t packets_captured = 0;

void signal_handler(int signum) {
    logger_info("Signal %d received, shutting down...", signum);
    is_running = 0;
}

void print_usage(const char *program_name) {
    fprintf(stdout, "Usage: %s [OPTIONS]\n", program_name);
    fprintf(stdout, "Options:\n");
    fprintf(stdout, "  -i INTERFACE    Network interface to monitor (default: eth0)\n");
    fprintf(stdout, "  -n COUNT        Number of packets to capture (default: unlimited)\n");
    fprintf(stdout, "  -t THREADS      Number of processing threads (default: 4)\n");
    fprintf(stdout, "  -d              Enable debug logging\n");
    fprintf(stdout, "  -h              Print this help message\n");
    fprintf(stdout, "\nExample:\n");
    fprintf(stdout, "  sudo %s -i eth0 -n 100 -t 4\n", program_name);
}

int main(int argc, char *argv[]) {
    char interface_name[256] = "eth0";
    int num_threads = NUM_THREADS;
    uint32_t max_packets = PACKETS_TO_CAPTURE;
    log_level_t log_level = LOG_INFO;

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            strncpy(interface_name, argv[++i], sizeof(interface_name) - 1);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            max_packets = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            num_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0) {
            log_level = LOG_DEBUG;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    /* Initialize logger */
    logger_init(NULL, log_level);
    logger_info("=== Network Packet Analyzer Started ===");
    logger_info("Interface: %s, Threads: %d, Max Packets: %s",
                interface_name, num_threads, max_packets ? "limited" : "unlimited");

    /* Register signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize socket */
    socket_config = socket_init(interface_name);
    if (socket_config == NULL) {
        logger_critical("Failed to initialize socket configuration");
        return 1;
    }

    if (socket_bind_raw(socket_config) < 0) {
        logger_critical("Failed to create raw socket (run with sudo)");
        socket_cleanup(socket_config);
        return 1;
    }

    if (socket_enable_promiscuous(socket_config) < 0) {
        logger_critical("Failed to enable promiscuous mode");
        socket_cleanup(socket_config);
        return 1;
    }

    /* Initialize thread pool */
    thread_pool = thread_pool_create(num_threads, MAX_QUEUE_SIZE);
    if (thread_pool == NULL) {
        logger_critical("Failed to create thread pool");
        socket_cleanup(socket_config);
        return 1;
    }

    /* Allocate packet buffer */
    uint8_t *packet_buffer = (uint8_t *)malloc(MAX_PACKET_SIZE);
    if (packet_buffer == NULL) {
        logger_critical("Failed to allocate packet buffer");
        thread_pool_destroy(thread_pool);
        socket_cleanup(socket_config);
        return 1;
    }

    /* Main packet capture loop */
    logger_info("Starting packet capture...");
    while (is_running) {
        int packet_size = socket_receive_packet(socket_config, packet_buffer, MAX_PACKET_SIZE);
        
        if (packet_size < 0) {
            if (is_running) {
                logger_error("Error receiving packet");
            }
            continue;
        }

        packets_captured++;

        /* Create and enqueue packet */
        packet_t *packet = packet_create(packet_buffer, packet_size);
        if (packet != NULL) {
            if (thread_pool_enqueue(thread_pool, packet) < 0) {
                logger_warn("Failed to enqueue packet (queue full)");
                packet_free(packet);
            } else {
                logger_debug("Packet #%u enqueued (size: %d bytes)", packets_captured, packet_size);
            }
        }

        /* Check if we've reached the limit */
        if (max_packets > 0 && packets_captured >= max_packets) {
            logger_info("Reached packet capture limit (%u packets)", max_packets);
            is_running = 0;
        }
    }

    /* Cleanup */
    logger_info("Waiting for thread pool to finish processing...");
    sleep(2); /* Give threads time to process remaining packets */

    logger_info("Total packets captured: %u", packets_captured);
    logger_info("Total packets processed: %d", thread_pool_get_processed_count(thread_pool));

    free(packet_buffer);
    thread_pool_destroy(thread_pool);
    socket_cleanup(socket_config);

    logger_info("=== Network Packet Analyzer Stopped ===");
    logger_cleanup();

    return 0;
}
