/**
 * @file regression.c
 * @brief Baseline comparison and regression detection implementation
 * 
 * Provides lightweight JSON parsing for baseline loading and
 * regression detection logic for CI performance gating.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <inttypes.h>
#include "regression.h"
#include "logger.h"

/* Maximum line length for JSON parsing */
#define MAX_LINE_LENGTH 1024

/* ============================================================================
 * Lightweight JSON Parsing Helpers
 * ============================================================================ */

/**
 * @brief Skip whitespace in string
 */
static const char* skip_whitespace(const char *p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

/**
 * @brief Extract a double value from JSON key-value pair
 * 
 * Searches for "key": value pattern and extracts the numeric value.
 */
static int json_extract_double(const char *json, const char *key, double *value) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return -1;
    }
    
    /* Move past the key and find the colon */
    pos += strlen(pattern);
    pos = skip_whitespace(pos);
    if (*pos != ':') {
        return -1;
    }
    pos++;
    pos = skip_whitespace(pos);
    
    /* Handle quoted numeric strings */
    if (*pos == '"') {
        pos++;
        *value = strtod(pos, NULL);
    } else {
        *value = strtod(pos, NULL);
    }
    
    return 0;
}

/**
 * @brief Extract a uint64 value from JSON key-value pair
 */
static int json_extract_uint64(const char *json, const char *key, uint64_t *value) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return -1;
    }
    
    /* Move past the key and find the colon */
    pos += strlen(pattern);
    pos = skip_whitespace(pos);
    if (*pos != ':') {
        return -1;
    }
    pos++;
    pos = skip_whitespace(pos);
    
    /* Handle quoted numeric strings */
    if (*pos == '"') {
        pos++;
    }
    
    *value = strtoull(pos, NULL, 10);
    return 0;
}

/**
 * @brief Extract a string value from JSON key-value pair
 */
static int json_extract_string(const char *json, const char *key, char *value, size_t value_len) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return -1;
    }
    
    /* Move past the key and find the colon */
    pos += strlen(pattern);
    pos = skip_whitespace(pos);
    if (*pos != ':') {
        return -1;
    }
    pos++;
    pos = skip_whitespace(pos);
    
    /* Expect opening quote */
    if (*pos != '"') {
        return -1;
    }
    pos++;
    
    /* Copy until closing quote */
    size_t i = 0;
    while (*pos && *pos != '"' && i < value_len - 1) {
        value[i++] = *pos++;
    }
    value[i] = '\0';
    
    return 0;
}

/**
 * @brief Extract an int value from JSON key-value pair
 */
static int json_extract_int(const char *json, const char *key, int *value) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return -1;
    }
    
    /* Move past the key and find the colon */
    pos += strlen(pattern);
    pos = skip_whitespace(pos);
    if (*pos != ':') {
        return -1;
    }
    pos++;
    pos = skip_whitespace(pos);
    
    *value = atoi(pos);
    return 0;
}

/**
 * @brief Read entire file into memory
 */
static char* read_file_contents(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (fp == NULL) {
        return NULL;
    }
    
    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 1024 * 1024) { /* 1MB max */
        fclose(fp);
        return NULL;
    }
    
    char *content = (char *)malloc(file_size + 1);
    if (content == NULL) {
        fclose(fp);
        return NULL;
    }
    
    size_t bytes_read = fread(content, 1, file_size, fp);
    content[bytes_read] = '\0';
    fclose(fp);
    
    return content;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

int regression_load_baseline(const char *filepath, regression_baseline_t *baseline) {
    if (filepath == NULL || baseline == NULL) {
        return -1;
    }
    
    /* Initialize baseline to invalid state */
    memset(baseline, 0, sizeof(regression_baseline_t));
    baseline->valid = false;
    
    /* Read JSON file */
    char *json = read_file_contents(filepath);
    if (json == NULL) {
        logger_error("Failed to read baseline file: %s", filepath);
        return -1;
    }
    
    /* Extract elapsed time */
    if (json_extract_double(json, "elapsed_sec", &baseline->elapsed_sec) != 0) {
        logger_warn("Failed to parse elapsed_sec from baseline");
    }
    
    /* Extract packets section */
    uint64_t pkts_processed = 0;
    if (json_extract_uint64(json, "processed", &pkts_processed) != 0) {
        logger_warn("Failed to parse packets.processed from baseline");
    }
    baseline->pkts_processed = pkts_processed;
    
    /* Extract rate_pps */
    if (json_extract_double(json, "rate_pps", &baseline->pkts_processed_per_sec) != 0) {
        /* Calculate from processed / elapsed if not present */
        if (baseline->elapsed_sec > 0) {
            baseline->pkts_processed_per_sec = (double)pkts_processed / baseline->elapsed_sec;
        } else {
            logger_warn("Failed to parse rate_pps from baseline");
        }
    }
    
    /* Extract bytes section */
    uint64_t bytes_processed = 0;
    if (json_extract_uint64(json, "bytes_processed", &bytes_processed) != 0) {
        /* Try alternate key name */
        const char *bytes_pos = strstr(json, "\"bytes\"");
        if (bytes_pos != NULL) {
            const char *processed_pos = strstr(bytes_pos, "\"processed\"");
            if (processed_pos != NULL && processed_pos < bytes_pos + 200) {
                json_extract_uint64(processed_pos - 1, "processed", &bytes_processed);
            }
        }
    }
    baseline->bytes_processed = bytes_processed;
    
    /* Extract rate_mbps */
    if (json_extract_double(json, "rate_mbps", &baseline->mbps_processed) != 0) {
        /* Calculate from bytes / elapsed if not present */
        if (baseline->elapsed_sec > 0 && bytes_processed > 0) {
            baseline->mbps_processed = (double)bytes_processed / baseline->elapsed_sec / (1024 * 1024);
        } else {
            logger_warn("Failed to parse rate_mbps from baseline");
        }
    }
    
    /* Extract latency p95 */
    if (json_extract_uint64(json, "p95", &baseline->latency_p95_ns) != 0) {
        logger_warn("Failed to parse latency_ns.p95 from baseline");
    }
    
    /* Extract drop counts */
    if (json_extract_uint64(json, "queue_drops", &baseline->queue_drops) != 0) {
        baseline->queue_drops = 0;
    }
    if (json_extract_uint64(json, "capture_drops", &baseline->capture_drops) != 0) {
        baseline->capture_drops = 0;
    }
    
    /* Calculate drop rate */
    uint64_t total_drops = baseline->queue_drops + baseline->capture_drops;
    uint64_t total_captured = 0;
    json_extract_uint64(json, "captured", &total_captured);
    
    if (total_captured > 0) {
        baseline->drop_rate = (double)total_drops / (double)total_captured;
    } else {
        baseline->drop_rate = 0.0;
    }
    
    /* Extract metadata section for compatibility validation */
    const char *metadata_pos = strstr(json, "\"metadata\"");
    if (metadata_pos != NULL) {
        memset(&baseline->metadata, 0, sizeof(baseline->metadata));
        
        json_extract_string(metadata_pos, "interface", 
                           baseline->metadata.interface, METRICS_META_STRING_LEN);
        json_extract_string(metadata_pos, "filter", 
                           baseline->metadata.filter, METRICS_META_STRING_LEN);
        json_extract_string(metadata_pos, "os", 
                           baseline->metadata.os, METRICS_META_STRING_LEN);
        json_extract_string(metadata_pos, "git_sha", 
                           baseline->metadata.git_sha, METRICS_META_STRING_LEN);
        json_extract_int(metadata_pos, "threads", &baseline->metadata.threads);
        json_extract_int(metadata_pos, "bpf_buffer_size", &baseline->metadata.bpf_buffer_size);
        json_extract_int(metadata_pos, "duration_sec", &baseline->metadata.duration_sec);
        json_extract_int(metadata_pos, "warmup_sec", &baseline->metadata.warmup_sec);
        json_extract_string(metadata_pos, "traffic_mode", 
                           baseline->metadata.traffic_mode, METRICS_META_STRING_LEN);
        json_extract_string(metadata_pos, "traffic_target", 
                           baseline->metadata.traffic_target, METRICS_META_STRING_LEN);
        json_extract_int(metadata_pos, "traffic_rate", &baseline->metadata.traffic_rate);
        
        baseline->metadata.valid = true;
        logger_debug("Loaded baseline metadata: interface=%s, filter=%s, threads=%d, os=%s, traffic=%s@%d",
                    baseline->metadata.interface,
                    baseline->metadata.filter,
                    baseline->metadata.threads,
                    baseline->metadata.os,
                    baseline->metadata.traffic_mode,
                    baseline->metadata.traffic_rate);
    } else {
        logger_warn("Baseline file missing metadata section (older format)");
        baseline->metadata.valid = false;
    }
    
    free(json);
    
    /* Mark as valid if we got minimum required fields */
    if (baseline->pkts_processed_per_sec > 0 || baseline->pkts_processed > 0) {
        baseline->valid = true;
        logger_info("Loaded baseline: %.2f pps, %.4f MB/s, p95=%" PRIu64 " ns, drop_rate=%.4f",
                    baseline->pkts_processed_per_sec,
                    baseline->mbps_processed,
                    baseline->latency_p95_ns,
                    baseline->drop_rate);
        return 0;
    }
    
    logger_error("Baseline file missing required metrics");
    return -1;
}

int regression_compare(const regression_baseline_t *baseline,
                       const metrics_snapshot_t *current,
                       double threshold,
                       regression_result_t *result) {
    if (baseline == NULL || current == NULL || result == NULL) {
        return -1;
    }
    
    if (!baseline->valid) {
        logger_error("Cannot compare against invalid baseline");
        return -1;
    }
    
    memset(result, 0, sizeof(regression_result_t));
    result->threshold = threshold;
    
    /* Calculate current metrics using capture_elapsed_sec (excludes drain time) */
    double current_pps = current->capture_elapsed_sec > 0 ? 
        (double)current->pkts_processed / current->capture_elapsed_sec : 0;
    double current_mbps = current->capture_elapsed_sec > 0 ?
        (double)current->bytes_processed / current->capture_elapsed_sec / (1024 * 1024) : 0;
    uint64_t current_p95 = metrics_percentile_ns(current, 0.95);
    
    uint64_t total_drops = current->queue_drops + current->capture_drops;
    double current_drop_rate = current->pkts_captured > 0 ?
        (double)total_drops / (double)current->pkts_captured : 0;
    
    /* Store values for reporting */
    result->baseline_pps = baseline->pkts_processed_per_sec;
    result->current_pps = current_pps;
    result->baseline_mbps = baseline->mbps_processed;
    result->current_mbps = current_mbps;
    result->baseline_p95_ns = baseline->latency_p95_ns;
    result->current_p95_ns = current_p95;
    result->baseline_drop_rate = baseline->drop_rate;
    result->current_drop_rate = current_drop_rate;
    
    /* Throughput regression: current < baseline * (1 - threshold) */
    if (result->baseline_pps > 0) {
        result->pps_delta_pct = (current_pps - result->baseline_pps) / result->baseline_pps;
        result->pps_regression = (current_pps < result->baseline_pps * (1.0 - threshold));
    }
    
    if (result->baseline_mbps > 0) {
        result->mbps_delta_pct = (current_mbps - result->baseline_mbps) / result->baseline_mbps;
        result->mbps_regression = (current_mbps < result->baseline_mbps * (1.0 - threshold));
    }
    
    /* Latency regression: current > baseline * (1 + threshold) */
    if (result->baseline_p95_ns > 0) {
        result->latency_delta_pct = ((double)current_p95 - (double)result->baseline_p95_ns) / 
                                     (double)result->baseline_p95_ns;
        result->latency_regression = (current_p95 > (uint64_t)(result->baseline_p95_ns * (1.0 + threshold)));
    }
    
    /* Drop rate regression: current > baseline * (1 + threshold) */
    /* Special case: if baseline had no drops, any drops > threshold of packets is regression */
    if (result->baseline_drop_rate > 0) {
        result->drop_delta_pct = (current_drop_rate - result->baseline_drop_rate) / 
                                  result->baseline_drop_rate;
        result->drop_regression = (current_drop_rate > result->baseline_drop_rate * (1.0 + threshold));
    } else {
        /* Baseline had no drops - regression if current drop rate exceeds threshold */
        result->drop_delta_pct = current_drop_rate > 0 ? INFINITY : 0;
        result->drop_regression = (current_drop_rate > threshold);
    }
    
    /* Check for any regression */
    result->any_regression = result->pps_regression || 
                             result->mbps_regression ||
                             result->latency_regression || 
                             result->drop_regression;
    
    return 0;
}

/**
 * @brief Format a percentage delta with color indicator
 */
static const char* format_delta(double delta_pct, bool is_regression, char *buf, size_t buflen) {
    const char *indicator = is_regression ? "FAIL" : "OK";
    const char *sign = delta_pct >= 0 ? "+" : "";
    
    if (isinf(delta_pct)) {
        snprintf(buf, buflen, "[%s] N/A (baseline was 0)", indicator);
    } else {
        snprintf(buf, buflen, "[%s] %s%.2f%%", indicator, sign, delta_pct * 100);
    }
    
    return buf;
}

/**
 * @brief Format latency value to human readable string
 */
static void format_latency_ns(uint64_t ns, char *buf, size_t buflen) {
    if (ns < 1000) {
        snprintf(buf, buflen, "%" PRIu64 " ns", ns);
    } else if (ns < 1000000) {
        snprintf(buf, buflen, "%.2f µs", ns / 1000.0);
    } else if (ns < 1000000000) {
        snprintf(buf, buflen, "%.2f ms", ns / 1000000.0);
    } else {
        snprintf(buf, buflen, "%.2f s", ns / 1000000000.0);
    }
}

void regression_print_report(const regression_result_t *result) {
    if (result == NULL) {
        return;
    }
    
    char delta_buf[64];
    char lat_baseline[32], lat_current[32];
    
    fprintf(stdout, "\n");
    fprintf(stdout, "================================================================================\n");
    fprintf(stdout, "                         REGRESSION ANALYSIS REPORT\n");
    fprintf(stdout, "================================================================================\n");
    fprintf(stdout, "Threshold: %.1f%%\n\n", result->threshold * 100);
    
    /* Throughput (PPS) */
    fprintf(stdout, "THROUGHPUT (packets/sec):\n");
    fprintf(stdout, "  Baseline:  %12.2f pps\n", result->baseline_pps);
    fprintf(stdout, "  Current:   %12.2f pps\n", result->current_pps);
    fprintf(stdout, "  Delta:     %s\n\n", 
            format_delta(result->pps_delta_pct, result->pps_regression, delta_buf, sizeof(delta_buf)));
    
    /* Throughput (MB/s) */
    fprintf(stdout, "THROUGHPUT (MB/sec):\n");
    fprintf(stdout, "  Baseline:  %12.4f MB/s\n", result->baseline_mbps);
    fprintf(stdout, "  Current:   %12.4f MB/s\n", result->current_mbps);
    fprintf(stdout, "  Delta:     %s\n\n",
            format_delta(result->mbps_delta_pct, result->mbps_regression, delta_buf, sizeof(delta_buf)));
    
    /* Latency P95 */
    format_latency_ns(result->baseline_p95_ns, lat_baseline, sizeof(lat_baseline));
    format_latency_ns(result->current_p95_ns, lat_current, sizeof(lat_current));
    fprintf(stdout, "LATENCY (p95):\n");
    fprintf(stdout, "  Baseline:  %12s\n", lat_baseline);
    fprintf(stdout, "  Current:   %12s\n", lat_current);
    fprintf(stdout, "  Delta:     %s\n\n",
            format_delta(result->latency_delta_pct, result->latency_regression, delta_buf, sizeof(delta_buf)));
    
    /* Drop Rate */
    fprintf(stdout, "DROP RATE:\n");
    fprintf(stdout, "  Baseline:  %12.4f%%\n", result->baseline_drop_rate * 100);
    fprintf(stdout, "  Current:   %12.4f%%\n", result->current_drop_rate * 100);
    fprintf(stdout, "  Delta:     %s\n\n",
            format_delta(result->drop_delta_pct, result->drop_regression, delta_buf, sizeof(delta_buf)));
    
    /* Summary */
    fprintf(stdout, "================================================================================\n");
    if (result->any_regression) {
        fprintf(stdout, "RESULT: PERFORMANCE REGRESSION DETECTED\n");
        fprintf(stdout, "  Regressions found in:");
        if (result->pps_regression) fprintf(stdout, " [throughput-pps]");
        if (result->mbps_regression) fprintf(stdout, " [throughput-mbps]");
        if (result->latency_regression) fprintf(stdout, " [latency-p95]");
        if (result->drop_regression) fprintf(stdout, " [drop-rate]");
        fprintf(stdout, "\n");
    } else {
        fprintf(stdout, "RESULT: ALL METRICS WITHIN THRESHOLD\n");
    }
    fprintf(stdout, "================================================================================\n\n");
    
    fflush(stdout);
}

bool regression_detected(const regression_result_t *result) {
    if (result == NULL) {
        return false;
    }
    return result->any_regression;
}

bool regression_validate_metadata(const regression_baseline_t *baseline,
                                  const metrics_metadata_t *current_meta,
                                  char *error_msg, size_t error_msg_len) {
    if (baseline == NULL || current_meta == NULL) {
        if (error_msg && error_msg_len > 0) {
            snprintf(error_msg, error_msg_len, "Invalid baseline or metadata pointer");
        }
        return false;
    }
    
    /* If baseline has no metadata, allow comparison with warning */
    if (!baseline->metadata.valid) {
        logger_warn("Baseline has no metadata - skipping compatibility check");
        return true;
    }
    
    /* Collect all mismatches for comprehensive reporting */
    bool has_hard_mismatch = false;  /* Must-match fields */
    int hard_mismatch_count = 0;
    int warn_mismatch_count = 0;
    
    /* Track individual field mismatches - MUST MATCH fields */
    bool mismatch_filter = false;
    bool mismatch_threads = false;
    bool mismatch_warmup_sec = false;
    bool mismatch_duration_sec = false;
    bool mismatch_traffic_mode = false;
    bool mismatch_traffic_target = false;
    bool mismatch_traffic_rate = false;
    
    /* Track individual field mismatches - WARN ONLY fields */
    bool mismatch_interface = false;
    bool mismatch_os = false;
    bool mismatch_bpf_buffer_size = false;
    
    /* === WARN-ONLY FIELDS (don't fail, just log) === */
    
    /* Interface - warn only (CI auto-detect may differ) */
    if (strlen(baseline->metadata.interface) > 0 && 
        strcmp(baseline->metadata.interface, current_meta->interface) != 0) {
        mismatch_interface = true;
        warn_mismatch_count++;
        logger_warn("Interface differs: baseline='%s', current='%s' (allowed)",
                   baseline->metadata.interface, current_meta->interface);
    }
    
    /* OS - warn only */
    if (strlen(baseline->metadata.os) > 0 && 
        strcmp(baseline->metadata.os, current_meta->os) != 0) {
        mismatch_os = true;
        warn_mismatch_count++;
        logger_warn("OS differs: baseline='%s', current='%s' (allowed)",
                   baseline->metadata.os, current_meta->os);
    }
    
    /* BPF buffer size - warn only (platform-specific) */
    if (baseline->metadata.bpf_buffer_size > 0 && 
        baseline->metadata.bpf_buffer_size != current_meta->bpf_buffer_size) {
        mismatch_bpf_buffer_size = true;
        warn_mismatch_count++;
        logger_warn("BPF buffer size differs: baseline=%d, current=%d (allowed)",
                   baseline->metadata.bpf_buffer_size, current_meta->bpf_buffer_size);
    }
    
    /* === MUST-MATCH FIELDS === */
    
    /* Filter - must match */
    if (strlen(baseline->metadata.filter) > 0 && 
        strcmp(baseline->metadata.filter, current_meta->filter) != 0) {
        mismatch_filter = true;
        has_hard_mismatch = true;
        hard_mismatch_count++;
    }
    
    /* Threads - must match */
    if (baseline->metadata.threads > 0 && 
        baseline->metadata.threads != current_meta->threads) {
        mismatch_threads = true;
        has_hard_mismatch = true;
        hard_mismatch_count++;
    }
    
    /* Warmup sec - must match */
    if (baseline->metadata.warmup_sec > 0 && 
        baseline->metadata.warmup_sec != current_meta->warmup_sec) {
        mismatch_warmup_sec = true;
        has_hard_mismatch = true;
        hard_mismatch_count++;
    }
    
    /* Duration sec - must match */
    if (baseline->metadata.duration_sec > 0 && 
        baseline->metadata.duration_sec != current_meta->duration_sec) {
        mismatch_duration_sec = true;
        has_hard_mismatch = true;
        hard_mismatch_count++;
    }
    
    /* Traffic mode - must match */
    if (strlen(baseline->metadata.traffic_mode) > 0 && 
        strcmp(baseline->metadata.traffic_mode, current_meta->traffic_mode) != 0) {
        mismatch_traffic_mode = true;
        has_hard_mismatch = true;
        hard_mismatch_count++;
    }
    
    /* Traffic target - must match */
    if (strlen(baseline->metadata.traffic_target) > 0 && 
        strcmp(baseline->metadata.traffic_target, current_meta->traffic_target) != 0) {
        mismatch_traffic_target = true;
        has_hard_mismatch = true;
        hard_mismatch_count++;
    }
    
    /* Traffic rate - must match */
    if (baseline->metadata.traffic_rate > 0 && 
        baseline->metadata.traffic_rate != current_meta->traffic_rate) {
        mismatch_traffic_rate = true;
        has_hard_mismatch = true;
        hard_mismatch_count++;
    }
    
    /* Print comprehensive mismatch report if any HARD mismatches found */
    if (has_hard_mismatch) {
        fprintf(stderr, "\n");
        fprintf(stderr, "================================================================================\n");
        fprintf(stderr, "                       BASELINE METADATA MISMATCH\n");
        fprintf(stderr, "================================================================================\n");
        fprintf(stderr, "Cannot compare against baseline: %d MUST-MATCH field(s) differ.\n", hard_mismatch_count);
        if (warn_mismatch_count > 0) {
            fprintf(stderr, "Additionally, %d WARN-ONLY field(s) differ (these are allowed).\n", warn_mismatch_count);
        }
        fprintf(stderr, "\n");
        fprintf(stderr, "%-20s %-25s %-25s %s\n", "FIELD", "BASELINE", "CURRENT", "STATUS");
        fprintf(stderr, "--------------------------------------------------------------------------------\n");
        
        /* === MUST-MATCH fields first === */
        
        /* Filter */
        fprintf(stderr, "%-20s %-25s %-25s %s\n", 
                "filter",
                baseline->metadata.filter[0] ? baseline->metadata.filter : "(not set)",
                current_meta->filter[0] ? current_meta->filter : "(not set)",
                mismatch_filter ? "[MISMATCH]" : "[OK]");
        
        /* Threads */
        char baseline_threads[32], current_threads[32];
        snprintf(baseline_threads, sizeof(baseline_threads), "%d", baseline->metadata.threads);
        snprintf(current_threads, sizeof(current_threads), "%d", current_meta->threads);
        fprintf(stderr, "%-20s %-25s %-25s %s\n", 
                "threads",
                baseline->metadata.threads > 0 ? baseline_threads : "(not set)",
                current_threads,
                mismatch_threads ? "[MISMATCH]" : "[OK]");
        
        /* Warmup sec */
        char baseline_warmup[32], current_warmup[32];
        snprintf(baseline_warmup, sizeof(baseline_warmup), "%d", baseline->metadata.warmup_sec);
        snprintf(current_warmup, sizeof(current_warmup), "%d", current_meta->warmup_sec);
        fprintf(stderr, "%-20s %-25s %-25s %s\n", 
                "warmup_sec",
                baseline->metadata.warmup_sec > 0 ? baseline_warmup : "(not set)",
                current_warmup,
                mismatch_warmup_sec ? "[MISMATCH]" : "[OK]");
        
        /* Duration sec */
        char baseline_duration[32], current_duration[32];
        snprintf(baseline_duration, sizeof(baseline_duration), "%d", baseline->metadata.duration_sec);
        snprintf(current_duration, sizeof(current_duration), "%d", current_meta->duration_sec);
        fprintf(stderr, "%-20s %-25s %-25s %s\n", 
                "duration_sec",
                baseline->metadata.duration_sec > 0 ? baseline_duration : "(not set)",
                current_duration,
                mismatch_duration_sec ? "[MISMATCH]" : "[OK]");
        
        /* Traffic mode */
        fprintf(stderr, "%-20s %-25s %-25s %s\n", 
                "traffic_mode",
                baseline->metadata.traffic_mode[0] ? baseline->metadata.traffic_mode : "(not set)",
                current_meta->traffic_mode[0] ? current_meta->traffic_mode : "(not set)",
                mismatch_traffic_mode ? "[MISMATCH]" : "[OK]");
        
        /* Traffic target */
        fprintf(stderr, "%-20s %-25s %-25s %s\n", 
                "traffic_target",
                baseline->metadata.traffic_target[0] ? baseline->metadata.traffic_target : "(not set)",
                current_meta->traffic_target[0] ? current_meta->traffic_target : "(not set)",
                mismatch_traffic_target ? "[MISMATCH]" : "[OK]");
        
        /* Traffic rate */
        char baseline_rate[32], current_rate[32];
        snprintf(baseline_rate, sizeof(baseline_rate), "%d", baseline->metadata.traffic_rate);
        snprintf(current_rate, sizeof(current_rate), "%d", current_meta->traffic_rate);
        fprintf(stderr, "%-20s %-25s %-25s %s\n", 
                "traffic_rate",
                baseline->metadata.traffic_rate > 0 ? baseline_rate : "(not set)",
                current_rate,
                mismatch_traffic_rate ? "[MISMATCH]" : "[OK]");
        
        fprintf(stderr, "--------------------------------------------------------------------------------\n");
        fprintf(stderr, "WARN-ONLY fields (mismatches allowed):\n");
        
        /* Interface */
        fprintf(stderr, "%-20s %-25s %-25s %s\n", 
                "interface",
                baseline->metadata.interface[0] ? baseline->metadata.interface : "(not set)",
                current_meta->interface[0] ? current_meta->interface : "(not set)",
                mismatch_interface ? "[WARN]" : "[OK]");
        
        /* OS */
        fprintf(stderr, "%-20s %-25s %-25s %s\n", 
                "os",
                baseline->metadata.os[0] ? baseline->metadata.os : "(not set)",
                current_meta->os[0] ? current_meta->os : "(not set)",
                mismatch_os ? "[WARN]" : "[OK]");
        
        /* BPF buffer size */
        char baseline_bpf[32], current_bpf[32];
        snprintf(baseline_bpf, sizeof(baseline_bpf), "%d", baseline->metadata.bpf_buffer_size);
        snprintf(current_bpf, sizeof(current_bpf), "%d", current_meta->bpf_buffer_size);
        fprintf(stderr, "%-20s %-25s %-25s %s\n", 
                "bpf_buffer_size",
                baseline->metadata.bpf_buffer_size > 0 ? baseline_bpf : "(not set)",
                current_bpf,
                mismatch_bpf_buffer_size ? "[WARN]" : "[OK]");
        
        fprintf(stderr, "================================================================================\n");
        fprintf(stderr, "Ensure baseline was generated with the same configuration as the current run.\n");
        fprintf(stderr, "Exit code: %d (CONFIG_MISMATCH)\n", EXIT_CONFIG_MISMATCH);
        fprintf(stderr, "================================================================================\n\n");
        fflush(stderr);
        
        /* Build error message for caller */
        if (error_msg && error_msg_len > 0) {
            snprintf(error_msg, error_msg_len, 
                    "Metadata mismatch: %d MUST-MATCH field(s) differ between baseline and current run",
                    hard_mismatch_count);
        }
        return false;
    }
    
    /* Git SHA - log for reference but don't block */
    if (strlen(baseline->metadata.git_sha) > 0 && 
        strcmp(baseline->metadata.git_sha, current_meta->git_sha) != 0) {
        logger_info("Note: Git SHA differs: baseline='%s', current='%s'",
                    baseline->metadata.git_sha, current_meta->git_sha);
        /* Allow - informational only */
    }
    
    return true;
}
