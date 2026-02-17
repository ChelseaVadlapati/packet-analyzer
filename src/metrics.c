/**
 * @file metrics.c
 * @brief Production-quality performance metrics implementation
 * 
 * Thread-safe, lock-free metrics tracking using C11 atomics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <sys/utsname.h>
#include "metrics.h"

/* Build git SHA - defined at compile time via -DGIT_SHA="..." */
#ifndef GIT_SHA
#define GIT_SHA "unknown"
#endif

/* Global metrics instance */
static metrics_t g_metrics;

/* Global metadata instance */
static metrics_metadata_t g_metadata;

/* ============================================================================
 * Core Functions
 * ============================================================================ */

void metrics_init(void) {
    memset(&g_metrics, 0, sizeof(metrics_t));
    
    /* Explicitly initialize all atomics to zero */
    atomic_store(&g_metrics.pkts_captured, 0);
    atomic_store(&g_metrics.pkts_processed, 0);
    atomic_store(&g_metrics.bytes_captured, 0);
    atomic_store(&g_metrics.bytes_processed, 0);
    
    atomic_store(&g_metrics.parse_errors, 0);
    atomic_store(&g_metrics.checksum_failures, 0);
    atomic_store(&g_metrics.queue_drops, 0);
    atomic_store(&g_metrics.capture_drops, 0);
    
    atomic_store(&g_metrics.ether_ipv4, 0);
    atomic_store(&g_metrics.ether_ipv6, 0);
    atomic_store(&g_metrics.ether_arp, 0);
    atomic_store(&g_metrics.ether_other, 0);
    
    atomic_store(&g_metrics.proto_tcp, 0);
    atomic_store(&g_metrics.proto_udp, 0);
    atomic_store(&g_metrics.proto_icmp, 0);
    atomic_store(&g_metrics.proto_other, 0);
    
    atomic_store(&g_metrics.queue_depth_max, 0);
    
    atomic_store(&g_metrics.latency_count, 0);
    atomic_store(&g_metrics.latency_sum_ns, 0);
    atomic_store(&g_metrics.latency_max_ns, 0);
    
    for (int i = 0; i < METRICS_HISTOGRAM_BUCKETS; i++) {
        atomic_store(&g_metrics.latency_histogram[i], 0);
    }
    
    g_metrics.start_time_ns = 0;
    g_metrics.capture_end_time_ns = 0;
}

void metrics_start(void) {
    g_metrics.start_time_ns = metrics_now_ns();
}

void metrics_stop_capture(void) {
    g_metrics.capture_end_time_ns = metrics_now_ns();
}

bool metrics_is_active(void) {
    return g_metrics.start_time_ns > 0;
}

uint64_t metrics_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ============================================================================
 * Recording Functions
 * ============================================================================ */

/**
 * @brief Calculate histogram bucket index for a latency value
 * 
 * Uses exponential bucketing: bucket i covers [2^i µs, 2^(i+1) µs)
 * Bucket 0: [0, 1µs), Bucket 1: [1µs, 2µs), etc.
 */
static inline int latency_bucket(uint64_t latency_ns) {
    if (latency_ns == 0) return 0;
    
    /* Convert to microseconds for bucket calculation */
    uint64_t latency_us = latency_ns / 1000;
    if (latency_us == 0) return 0;
    
    /* Find highest set bit (log2) */
    int bucket = 0;
    uint64_t val = latency_us;
    while (val > 1 && bucket < METRICS_HISTOGRAM_BUCKETS - 1) {
        val >>= 1;
        bucket++;
    }
    
    return bucket;
}

void metrics_observe_latency(uint64_t latency_ns) {
    /* Update count */
    atomic_fetch_add(&g_metrics.latency_count, 1);
    
    /* Update sum */
    atomic_fetch_add(&g_metrics.latency_sum_ns, latency_ns);
    
    /* Update max using compare-exchange loop */
    uint64_t current_max = atomic_load(&g_metrics.latency_max_ns);
    while (latency_ns > current_max) {
        if (atomic_compare_exchange_weak(&g_metrics.latency_max_ns, 
                                          &current_max, latency_ns)) {
            break;
        }
    }
    
    /* Update histogram */
    int bucket = latency_bucket(latency_ns);
    atomic_fetch_add(&g_metrics.latency_histogram[bucket], 1);
}

void metrics_record_protocol(uint8_t protocol) {
    switch (protocol) {
        case PROTO_TCP:
            atomic_fetch_add(&g_metrics.proto_tcp, 1);
            break;
        case PROTO_UDP:
            atomic_fetch_add(&g_metrics.proto_udp, 1);
            break;
        case PROTO_ICMP:
        case PROTO_ICMPV6:
            atomic_fetch_add(&g_metrics.proto_icmp, 1);
            break;
        default:
            atomic_fetch_add(&g_metrics.proto_other, 1);
            break;
    }
}

void metrics_record_ethertype(uint16_t ethertype) {
    switch (ethertype) {
        case ETHER_IPV4:
            atomic_fetch_add(&g_metrics.ether_ipv4, 1);
            break;
        case ETHER_IPV6:
            atomic_fetch_add(&g_metrics.ether_ipv6, 1);
            break;
        case ETHER_ARP:
            atomic_fetch_add(&g_metrics.ether_arp, 1);
            break;
        default:
            atomic_fetch_add(&g_metrics.ether_other, 1);
            break;
    }
}

void metrics_inc_captured(uint32_t bytes) {
    atomic_fetch_add(&g_metrics.pkts_captured, 1);
    atomic_fetch_add(&g_metrics.bytes_captured, bytes);
}

void metrics_inc_processed(uint32_t bytes) {
    atomic_fetch_add(&g_metrics.pkts_processed, 1);
    atomic_fetch_add(&g_metrics.bytes_processed, bytes);
}

void metrics_inc_parse_errors(void) {
    atomic_fetch_add(&g_metrics.parse_errors, 1);
}

void metrics_inc_checksum_failures(void) {
    atomic_fetch_add(&g_metrics.checksum_failures, 1);
}

void metrics_inc_queue_drops(void) {
    atomic_fetch_add(&g_metrics.queue_drops, 1);
}

void metrics_inc_capture_drops(void) {
    atomic_fetch_add(&g_metrics.capture_drops, 1);
}

void metrics_update_queue_depth_max(uint32_t current_depth) {
    uint32_t current_max = atomic_load(&g_metrics.queue_depth_max);
    while (current_depth > current_max) {
        if (atomic_compare_exchange_weak(&g_metrics.queue_depth_max,
                                          &current_max, current_depth)) {
            break;
        }
    }
}

/* ============================================================================
 * Reporting Functions
 * ============================================================================ */

void metrics_snapshot(metrics_snapshot_t *snapshot) {
    if (snapshot == NULL) return;
    
    snapshot->snapshot_time_ns = metrics_now_ns();
    snapshot->start_time_ns = g_metrics.start_time_ns;
    snapshot->capture_end_time_ns = g_metrics.capture_end_time_ns;
    
    if (snapshot->start_time_ns > 0) {
        snapshot->elapsed_sec = (double)(snapshot->snapshot_time_ns - snapshot->start_time_ns) / 1e9;
    } else {
        snapshot->elapsed_sec = 0.0;
    }
    
    /* Calculate capture-only elapsed time (excludes drain/shutdown time) */
    if (snapshot->start_time_ns > 0 && snapshot->capture_end_time_ns > 0) {
        snapshot->capture_elapsed_sec = (double)(snapshot->capture_end_time_ns - snapshot->start_time_ns) / 1e9;
    } else if (snapshot->start_time_ns > 0) {
        /* Capture still running, use snapshot time */
        snapshot->capture_elapsed_sec = snapshot->elapsed_sec;
    } else {
        snapshot->capture_elapsed_sec = 0.0;
    }
    
    /* Load all atomic values */
    snapshot->pkts_captured = atomic_load(&g_metrics.pkts_captured);
    snapshot->pkts_processed = atomic_load(&g_metrics.pkts_processed);
    snapshot->bytes_captured = atomic_load(&g_metrics.bytes_captured);
    snapshot->bytes_processed = atomic_load(&g_metrics.bytes_processed);
    
    snapshot->parse_errors = atomic_load(&g_metrics.parse_errors);
    snapshot->checksum_failures = atomic_load(&g_metrics.checksum_failures);
    snapshot->queue_drops = atomic_load(&g_metrics.queue_drops);
    snapshot->capture_drops = atomic_load(&g_metrics.capture_drops);
    
    snapshot->ether_ipv4 = atomic_load(&g_metrics.ether_ipv4);
    snapshot->ether_ipv6 = atomic_load(&g_metrics.ether_ipv6);
    snapshot->ether_arp = atomic_load(&g_metrics.ether_arp);
    snapshot->ether_other = atomic_load(&g_metrics.ether_other);
    
    snapshot->proto_tcp = atomic_load(&g_metrics.proto_tcp);
    snapshot->proto_udp = atomic_load(&g_metrics.proto_udp);
    snapshot->proto_icmp = atomic_load(&g_metrics.proto_icmp);
    snapshot->proto_other = atomic_load(&g_metrics.proto_other);
    
    snapshot->queue_depth_max = atomic_load(&g_metrics.queue_depth_max);
    
    snapshot->latency_count = atomic_load(&g_metrics.latency_count);
    snapshot->latency_sum_ns = atomic_load(&g_metrics.latency_sum_ns);
    snapshot->latency_max_ns = atomic_load(&g_metrics.latency_max_ns);
    
    for (int i = 0; i < METRICS_HISTOGRAM_BUCKETS; i++) {
        snapshot->latency_histogram[i] = atomic_load(&g_metrics.latency_histogram[i]);
    }
}

uint64_t metrics_percentile_ns(const metrics_snapshot_t *snapshot, double percentile) {
    if (snapshot == NULL || snapshot->latency_count == 0) {
        return 0;
    }
    
    uint64_t target_count = (uint64_t)(snapshot->latency_count * percentile);
    uint64_t cumulative = 0;
    
    for (int i = 0; i < METRICS_HISTOGRAM_BUCKETS; i++) {
        cumulative += snapshot->latency_histogram[i];
        if (cumulative >= target_count) {
            /* Return bucket midpoint in nanoseconds */
            /* Bucket i represents approximately [2^i µs, 2^(i+1) µs) */
            if (i == 0) {
                return 500; /* 0.5µs = 500ns */
            }
            uint64_t low_us = 1ULL << (i - 1);
            uint64_t high_us = 1ULL << i;
            return ((low_us + high_us) / 2) * 1000; /* Convert to ns */
        }
    }
    
    /* Return max if we reach here */
    return snapshot->latency_max_ns;
}

/**
 * @brief Format nanoseconds to human-readable string
 */
static void format_latency(uint64_t ns, char *buf, size_t buflen) {
    if (ns < 1000) {
        snprintf(buf, buflen, "%" PRIu64 "ns", ns);
    } else if (ns < 1000000) {
        snprintf(buf, buflen, "%.1fµs", ns / 1000.0);
    } else if (ns < 1000000000) {
        snprintf(buf, buflen, "%.2fms", ns / 1000000.0);
    } else {
        snprintf(buf, buflen, "%.2fs", ns / 1000000000.0);
    }
}

void metrics_print_human(void) {
    metrics_snapshot_t snap;
    metrics_snapshot(&snap);
    
    /* Use capture_elapsed_sec for throughput (excludes drain time) */
    double pps = snap.capture_elapsed_sec > 0 ? snap.pkts_processed / snap.capture_elapsed_sec : 0;
    double mbps = snap.capture_elapsed_sec > 0 ? (snap.bytes_processed / snap.capture_elapsed_sec) / (1024 * 1024) : 0;
    
    uint64_t total_drops = snap.queue_drops + snap.capture_drops;
    
    uint64_t p50 = metrics_percentile_ns(&snap, 0.50);
    uint64_t p95 = metrics_percentile_ns(&snap, 0.95);
    uint64_t p99 = metrics_percentile_ns(&snap, 0.99);
    
    char p50_str[32], p95_str[32], p99_str[32], max_str[32];
    format_latency(p50, p50_str, sizeof(p50_str));
    format_latency(p95, p95_str, sizeof(p95_str));
    format_latency(p99, p99_str, sizeof(p99_str));
    format_latency(snap.latency_max_ns, max_str, sizeof(max_str));
    
    fprintf(stdout, "[METRICS] %.1fs | pkts: %" PRIu64 " (%.0f/s) | %.2f MB/s | "
            "drops: %" PRIu64 " | latency p50/p95/p99/max: %s/%s/%s/%s\n",
            snap.elapsed_sec,
            snap.pkts_processed,
            pps,
            mbps,
            total_drops,
            p50_str, p95_str, p99_str, max_str);
    
    /* Print protocol breakdown */
    fprintf(stdout, "[PROTO] L3: IPv4=%" PRIu64 " IPv6=%" PRIu64 " ARP=%" PRIu64 " other=%" PRIu64
            " | L4: TCP=%" PRIu64 " UDP=%" PRIu64 " ICMP=%" PRIu64 " other=%" PRIu64 "\n",
            snap.ether_ipv4, snap.ether_ipv6, snap.ether_arp, snap.ether_other,
            snap.proto_tcp, snap.proto_udp, snap.proto_icmp, snap.proto_other);
    
    fflush(stdout);
}

void metrics_print_live_stats(void) {
    metrics_snapshot_t snap;
    metrics_snapshot(&snap);
    
    /* Use capture_elapsed_sec for throughput (excludes drain time) */
    double pps = snap.capture_elapsed_sec > 0 ? snap.pkts_captured / snap.capture_elapsed_sec : 0;
    double mbps = snap.capture_elapsed_sec > 0 ? (snap.bytes_captured / snap.capture_elapsed_sec) / (1024 * 1024) : 0;
    
    uint64_t total_drops = snap.queue_drops + snap.capture_drops;
    
    fprintf(stdout, "[METRICS] t=%.1f pkts=%" PRIu64 " pps=%.0f MB/s=%.2f drops=%" PRIu64 "\n",
            snap.elapsed_sec,
            snap.pkts_captured,
            pps,
            mbps,
            total_drops);
    
    fflush(stdout);
}

int metrics_snapshot_json(const char *filepath) {
    if (filepath == NULL) return -1;
    
    metrics_snapshot_t snap;
    metrics_snapshot(&snap);
    
    FILE *fp = fopen(filepath, "w");
    if (fp == NULL) {
        return -1;
    }
    
    /* Use capture_elapsed_sec for throughput calculations (excludes drain time) */
    double pps = snap.capture_elapsed_sec > 0 ? snap.pkts_processed / snap.capture_elapsed_sec : 0;
    double mbps = snap.capture_elapsed_sec > 0 ? (snap.bytes_processed / snap.capture_elapsed_sec) / (1024 * 1024) : 0;
    
    uint64_t p50 = metrics_percentile_ns(&snap, 0.50);
    uint64_t p95 = metrics_percentile_ns(&snap, 0.95);
    uint64_t p99 = metrics_percentile_ns(&snap, 0.99);
    uint64_t avg_ns = snap.latency_count > 0 ? snap.latency_sum_ns / snap.latency_count : 0;
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"timestamp\": \"%.3f\",\n", snap.snapshot_time_ns / 1e9);
    fprintf(fp, "  \"elapsed_sec\": %.3f,\n", snap.elapsed_sec);
    fprintf(fp, "  \"capture_elapsed_sec\": %.3f,\n", snap.capture_elapsed_sec);
    fprintf(fp, "  \"packets\": {\n");
    fprintf(fp, "    \"captured\": %" PRIu64 ",\n", snap.pkts_captured);
    fprintf(fp, "    \"processed\": %" PRIu64 ",\n", snap.pkts_processed);
    fprintf(fp, "    \"rate_pps\": %.2f\n", pps);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"bytes\": {\n");
    fprintf(fp, "    \"captured\": %" PRIu64 ",\n", snap.bytes_captured);
    fprintf(fp, "    \"processed\": %" PRIu64 ",\n", snap.bytes_processed);
    fprintf(fp, "    \"rate_mbps\": %.4f\n", mbps);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"errors\": {\n");
    fprintf(fp, "    \"parse_errors\": %" PRIu64 ",\n", snap.parse_errors);
    fprintf(fp, "    \"checksum_failures\": %" PRIu64 ",\n", snap.checksum_failures);
    fprintf(fp, "    \"queue_drops\": %" PRIu64 ",\n", snap.queue_drops);
    fprintf(fp, "    \"capture_drops\": %" PRIu64 "\n", snap.capture_drops);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"ethertype\": {\n");
    fprintf(fp, "    \"ipv4\": %" PRIu64 ",\n", snap.ether_ipv4);
    fprintf(fp, "    \"ipv6\": %" PRIu64 ",\n", snap.ether_ipv6);
    fprintf(fp, "    \"arp\": %" PRIu64 ",\n", snap.ether_arp);
    fprintf(fp, "    \"other\": %" PRIu64 "\n", snap.ether_other);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"protocols\": {\n");
    fprintf(fp, "    \"tcp\": %" PRIu64 ",\n", snap.proto_tcp);
    fprintf(fp, "    \"udp\": %" PRIu64 ",\n", snap.proto_udp);
    fprintf(fp, "    \"icmp\": %" PRIu64 ",\n", snap.proto_icmp);
    fprintf(fp, "    \"other\": %" PRIu64 "\n", snap.proto_other);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"queue\": {\n");
    fprintf(fp, "    \"depth_max\": %" PRIu32 "\n", snap.queue_depth_max);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"latency_ns\": {\n");
    fprintf(fp, "    \"count\": %" PRIu64 ",\n", snap.latency_count);
    fprintf(fp, "    \"sum\": %" PRIu64 ",\n", snap.latency_sum_ns);
    fprintf(fp, "    \"avg\": %" PRIu64 ",\n", avg_ns);
    fprintf(fp, "    \"max\": %" PRIu64 ",\n", snap.latency_max_ns);
    fprintf(fp, "    \"p50\": %" PRIu64 ",\n", p50);
    fprintf(fp, "    \"p95\": %" PRIu64 ",\n", p95);
    fprintf(fp, "    \"p99\": %" PRIu64 "\n", p99);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"latency_histogram\": [\n");
    for (int i = 0; i < METRICS_HISTOGRAM_BUCKETS; i++) {
        fprintf(fp, "    %" PRIu64 "%s\n", 
                snap.latency_histogram[i],
                (i < METRICS_HISTOGRAM_BUCKETS - 1) ? "," : "");
    }
    fprintf(fp, "  ],\n");
    
    /* Include metadata for baseline compatibility validation */
    fprintf(fp, "  \"metadata\": {\n");
    fprintf(fp, "    \"interface\": \"%s\",\n", g_metadata.interface);
    fprintf(fp, "    \"filter\": \"%s\",\n", g_metadata.filter);
    fprintf(fp, "    \"threads\": %d,\n", g_metadata.threads);
    fprintf(fp, "    \"bpf_buffer_size\": %d,\n", g_metadata.bpf_buffer_size);
    fprintf(fp, "    \"duration_sec\": %d,\n", g_metadata.duration_sec);
    fprintf(fp, "    \"warmup_sec\": %d,\n", g_metadata.warmup_sec);
    fprintf(fp, "    \"traffic_mode\": \"%s\",\n", g_metadata.traffic_mode);
    fprintf(fp, "    \"traffic_target\": \"%s\",\n", g_metadata.traffic_target);
    fprintf(fp, "    \"traffic_rate\": %d,\n", g_metadata.traffic_rate);
    fprintf(fp, "    \"os\": \"%s\",\n", g_metadata.os);
    fprintf(fp, "    \"git_sha\": \"%s\"\n", g_metadata.git_sha);
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
    return 0;
}

void metrics_set_metadata(const char *interface, const char *filter,
                          int threads, int bpf_buffer_size,
                          int duration_sec, int warmup_sec,
                          const char *traffic_mode_param, const char *traffic_target_param,
                          int traffic_rate_param) {
    memset(&g_metadata, 0, sizeof(g_metadata));
    
    if (interface != NULL) {
        strncpy(g_metadata.interface, interface, METRICS_META_STRING_LEN - 1);
    }
    if (filter != NULL) {
        strncpy(g_metadata.filter, filter, METRICS_META_STRING_LEN - 1);
    } else {
        strncpy(g_metadata.filter, "none", METRICS_META_STRING_LEN - 1);
    }
    
    g_metadata.threads = threads;
    g_metadata.bpf_buffer_size = bpf_buffer_size;
    g_metadata.duration_sec = duration_sec;
    g_metadata.warmup_sec = warmup_sec;
    
    /* Traffic settings */
    if (traffic_mode_param != NULL) {
        strncpy(g_metadata.traffic_mode, traffic_mode_param, METRICS_META_STRING_LEN - 1);
    } else {
        strncpy(g_metadata.traffic_mode, "none", METRICS_META_STRING_LEN - 1);
    }
    if (traffic_target_param != NULL) {
        strncpy(g_metadata.traffic_target, traffic_target_param, METRICS_META_STRING_LEN - 1);
    }
    g_metadata.traffic_rate = traffic_rate_param;
    
    /* Get OS info */
    struct utsname uts;
    if (uname(&uts) == 0) {
        strncpy(g_metadata.os, uts.sysname, METRICS_META_STRING_LEN - 1);
    } else {
        strncpy(g_metadata.os, "unknown", METRICS_META_STRING_LEN - 1);
    }
    
    /* Set git SHA from compile-time define */
    strncpy(g_metadata.git_sha, GIT_SHA, METRICS_META_STRING_LEN - 1);
    
    g_metadata.valid = true;
}

const metrics_metadata_t* metrics_get_metadata(void) {
    return &g_metadata;
}

metrics_t* metrics_get(void) {
    return &g_metrics;
}
