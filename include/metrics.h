/**
 * @file metrics.h
 * @brief Production-quality performance metrics for packet analyzer
 * 
 * Provides thread-safe, lock-free atomic counters and latency tracking
 * suitable for SDET performance testing tools.
 */

#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>

/* Histogram configuration: 32 buckets for nanosecond latency tracking */
#define METRICS_HISTOGRAM_BUCKETS 32

/* Maximum string length for metadata fields */
#define METRICS_META_STRING_LEN 64

/**
 * @brief Baseline metadata for compatibility validation
 * 
 * Stores configuration parameters used during capture to ensure
 * baseline comparisons are made against equivalent test conditions.
 */
typedef struct {
    char interface[METRICS_META_STRING_LEN];
    char filter[METRICS_META_STRING_LEN];       /* e.g., "icmp", "none" */
    char os[METRICS_META_STRING_LEN];           /* e.g., "Darwin", "Linux" */
    char git_sha[METRICS_META_STRING_LEN];      /* Build commit hash */
    char traffic_mode[METRICS_META_STRING_LEN]; /* e.g., "icmp", "none" */
    char traffic_target[METRICS_META_STRING_LEN]; /* e.g., "8.8.8.8" */
    int threads;
    int bpf_buffer_size;
    int duration_sec;
    int warmup_sec;
    int traffic_rate;                           /* Traffic rate in pps (0 if disabled) */
    bool valid;
} metrics_metadata_t;

/* Histogram bucket boundaries (nanoseconds):
 * Bucket 0:  [0, 1µs)
 * Bucket 1:  [1µs, 2µs)
 * Bucket 2:  [2µs, 4µs)
 * ...
 * Bucket 31: [2^30 ns, infinity) ≈ [1s, infinity)
 */

/**
 * @brief Thread-safe atomic metrics structure
 * 
 * All counters use C11 atomics for lock-free concurrent access.
 * Uses _Atomic qualifier for proper memory ordering.
 */
typedef struct {
    /* Packet counters */
    _Atomic uint64_t pkts_captured;
    _Atomic uint64_t pkts_processed;
    _Atomic uint64_t bytes_captured;
    _Atomic uint64_t bytes_processed;

    /* Error counters */
    _Atomic uint64_t parse_errors;
    _Atomic uint64_t checksum_failures;
    _Atomic uint64_t queue_drops;
    _Atomic uint64_t capture_drops;

    /* EtherType counters (L2/L3) */
    _Atomic uint64_t ether_ipv4;
    _Atomic uint64_t ether_ipv6;
    _Atomic uint64_t ether_arp;
    _Atomic uint64_t ether_other;

    /* Protocol counters (L4) */
    _Atomic uint64_t proto_tcp;
    _Atomic uint64_t proto_udp;
    _Atomic uint64_t proto_icmp;
    _Atomic uint64_t proto_other;

    /* Queue tracking */
    _Atomic uint32_t queue_depth_max;

    /* Latency tracking (nanoseconds) */
    _Atomic uint64_t latency_count;
    _Atomic uint64_t latency_sum_ns;
    _Atomic uint64_t latency_max_ns;
    _Atomic uint64_t latency_histogram[METRICS_HISTOGRAM_BUCKETS];

    /* Timing */
    uint64_t start_time_ns;
    uint64_t capture_end_time_ns;  /* Set when capture loop ends */
} metrics_t;

/**
 * @brief Snapshot of metrics for reporting
 * 
 * Non-atomic copy of metrics taken at a point in time.
 * Used for consistent reporting without holding locks.
 */
typedef struct {
    uint64_t pkts_captured;
    uint64_t pkts_processed;
    uint64_t bytes_captured;
    uint64_t bytes_processed;
    
    uint64_t parse_errors;
    uint64_t checksum_failures;
    uint64_t queue_drops;
    uint64_t capture_drops;
    
    uint64_t ether_ipv4;
    uint64_t ether_ipv6;
    uint64_t ether_arp;
    uint64_t ether_other;
    
    uint64_t proto_tcp;
    uint64_t proto_udp;
    uint64_t proto_icmp;
    uint64_t proto_other;
    
    uint32_t queue_depth_max;
    
    uint64_t latency_count;
    uint64_t latency_sum_ns;
    uint64_t latency_max_ns;
    uint64_t latency_histogram[METRICS_HISTOGRAM_BUCKETS];
    
    uint64_t start_time_ns;
    uint64_t snapshot_time_ns;
    uint64_t capture_end_time_ns;
    double elapsed_sec;
    double capture_elapsed_sec;  /* Capture loop duration only (excludes drain time) */
} metrics_snapshot_t;

/* EtherType identifiers for metrics_record_ethertype */
typedef enum {
    ETHER_IPV4 = 0x0800,
    ETHER_IPV6 = 0x86DD,
    ETHER_ARP  = 0x0806
} ethertype_t;

/* Protocol identifiers for metrics_record_protocol */
typedef enum {
    PROTO_TCP  = 6,
    PROTO_UDP  = 17,
    PROTO_ICMP = 1,
    PROTO_ICMPV6 = 58
} protocol_type_t;

/* ============================================================================
 * Core Functions
 * ============================================================================ */

/**
 * @brief Initialize the global metrics subsystem
 * 
 * Resets all counters to zero. Must be called before any other metrics function.
 */
void metrics_init(void);

/**
 * @brief Mark the start time for metrics collection
 * 
 * Records the current monotonic timestamp as the start of metrics collection.
 * Call this when packet capture begins.
 */
void metrics_start(void);

/**
 * @brief Mark the end of packet capture
 * 
 * Records the timestamp when the capture loop ends.
 * Used to calculate capture_elapsed_sec (excludes drain/shutdown time).
 */
void metrics_stop_capture(void);

/**
 * @brief Check if metrics collection is active
 * 
 * @return true if metrics_start() has been called, false otherwise
 */
bool metrics_is_active(void);

/**
 * @brief Get current monotonic timestamp in nanoseconds
 * 
 * @return Current CLOCK_MONOTONIC time in nanoseconds
 */
uint64_t metrics_now_ns(void);

/* ============================================================================
 * Recording Functions (Thread-safe, lock-free)
 * ============================================================================ */

/**
 * @brief Record latency observation
 * 
 * Updates latency histogram, sum, count, and max atomically.
 * 
 * @param latency_ns Latency value in nanoseconds
 */
void metrics_observe_latency(uint64_t latency_ns);

/**
 * @brief Record protocol type for a processed packet
 * 
 * @param protocol IP protocol number (6=TCP, 17=UDP, 1=ICMP, other)
 */
void metrics_record_protocol(uint8_t protocol);

/**
 * @brief Record EtherType for a processed packet
 * 
 * @param ethertype Ethernet type (0x0800=IPv4, 0x86DD=IPv6, 0x0806=ARP, other)
 */
void metrics_record_ethertype(uint16_t ethertype);

/**
 * @brief Increment packets captured counter
 * 
 * @param bytes Number of bytes in the captured packet
 */
void metrics_inc_captured(uint32_t bytes);

/**
 * @brief Increment packets processed counter
 * 
 * @param bytes Number of bytes in the processed packet
 */
void metrics_inc_processed(uint32_t bytes);

/**
 * @brief Increment parse error counter
 */
void metrics_inc_parse_errors(void);

/**
 * @brief Increment checksum failure counter
 */
void metrics_inc_checksum_failures(void);

/**
 * @brief Increment queue drop counter
 */
void metrics_inc_queue_drops(void);

/**
 * @brief Increment capture drop counter
 */
void metrics_inc_capture_drops(void);

/**
 * @brief Update queue depth maximum watermark
 * 
 * Uses atomic compare-exchange to maintain maximum.
 * 
 * @param current_depth Current queue depth
 */
void metrics_update_queue_depth_max(uint32_t current_depth);

/* ============================================================================
 * Reporting Functions
 * ============================================================================ */

/**
 * @brief Take a consistent snapshot of all metrics
 * 
 * @param snapshot Pointer to snapshot structure to fill
 */
void metrics_snapshot(metrics_snapshot_t *snapshot);

/**
 * @brief Print one-line human-readable metrics summary
 * 
 * Outputs: packets/sec, MB/sec, drops, p50/p95/p99/max latency
 */
void metrics_print_human(void);

/**
 * @brief Print compact live stats line
 * 
 * Outputs: [METRICS] t=... pkts=... pps=... MB/s=... drops=...
 * Thread-safe using atomic counters.
 */
void metrics_print_live_stats(void);

/**
 * @brief Write full JSON metrics summary to file
 * 
 * @param filepath Path to output JSON file
 * @return 0 on success, -1 on error
 */
int metrics_snapshot_json(const char *filepath);

/**
 * @brief Set metadata for baseline compatibility tracking
 * 
 * Call this before writing JSON metrics to include configuration
 * information needed for baseline validation.
 * 
 * @param interface Network interface name
 * @param filter Filter string (e.g., "icmp" or "none")
 * @param threads Number of processing threads
 * @param bpf_buffer_size BPF buffer size (macOS only, 0 for default)
 * @param duration_sec Capture duration in seconds
 * @param warmup_sec Warmup period in seconds
 * @param traffic_mode Traffic mode (e.g., "icmp" or NULL for none)
 * @param traffic_target Traffic target IP (e.g., "8.8.8.8" or NULL)
 * @param traffic_rate Traffic rate in pps (0 if disabled)
 */
void metrics_set_metadata(const char *interface, const char *filter,
                          int threads, int bpf_buffer_size,
                          int duration_sec, int warmup_sec,
                          const char *traffic_mode, const char *traffic_target,
                          int traffic_rate);

/**
 * @brief Get current metadata
 * 
 * @return Pointer to metadata structure
 */
const metrics_metadata_t* metrics_get_metadata(void);

/**
 * @brief Calculate percentile latency from histogram
 * 
 * @param snapshot Metrics snapshot containing histogram
 * @param percentile Percentile to calculate (0.0 - 1.0)
 * @return Latency value in nanoseconds
 */
uint64_t metrics_percentile_ns(const metrics_snapshot_t *snapshot, double percentile);

/**
 * @brief Get pointer to global metrics structure
 * 
 * For direct atomic access when needed.
 * 
 * @return Pointer to global metrics
 */
metrics_t* metrics_get(void);

#endif /* METRICS_H */
