#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "packet.h"
#include "socket_handler.h"
#include "thread_pool.h"
#include "logger.h"
#include "parser.h"
#include "metrics.h"
#include "regression.h"

#define MAX_PACKET_SIZE 65535
#define NUM_THREADS 4
#define MAX_QUEUE_SIZE 100
#define PACKETS_TO_CAPTURE 0 /* 0 = unlimited */
#define EXIT_INSUFFICIENT_SAMPLE 3

static volatile int is_running = 1;
static socket_config_t *socket_config = NULL;
static thread_pool_t *thread_pool = NULL;
static uint32_t packets_captured = 0;

/* Metrics configuration */
static int duration_sec = 20;          /* default 20 seconds */
static int warmup_sec = 2;             /* default 2 seconds warmup */
static int measure_sec = 0;            /* 0 = use duration_sec */
static int num_runs = 5;               /* default 5 runs, use --runs to change */
static int metrics_interval_ms = 0;    /* 0 = disabled */
static int stats_interval_sec = 1;     /* default 1 second, 0 = disabled */
static uint32_t min_packets = 200;     /* minimum packets for valid run */
static char *metrics_json_path = NULL;

/* Regression configuration */
static char *baseline_path = NULL;
static int fail_on_regression = 0;
static double regression_threshold = REGRESSION_THRESHOLD_DEFAULT;

/* Filter configuration */
static int filter_icmp = 0;

/* Traffic generation configuration */
static char *traffic_mode = NULL;      /* "icmp" or NULL */
static char *traffic_target = NULL;    /* Target IP for traffic generation */
static int traffic_rate = 50;          /* Packets per second (default 50) */
static pid_t traffic_pid = -1;         /* PID of traffic generator process */

/**
 * @brief Start background traffic generator
 * 
 * Spawns a background process to generate traffic during warmup+measurement.
 * Currently supports ICMP (ping) mode with configurable rate.
 * 
 * @return 0 on success, -1 on error
 */
static int traffic_generator_start(void) {
    if (traffic_mode == NULL) {
        return 0;  /* No traffic generation requested */
    }
    
    if (strcmp(traffic_mode, "icmp") == 0) {
        const char *target = traffic_target ? traffic_target : "8.8.8.8";
        
        /* Calculate ping interval from rate (rate = packets/sec, interval = 1/rate) */
        /* Minimum interval for ping is typically 0.002 (500 pps) on macOS */
        double interval = 1.0 / (double)traffic_rate;
        if (interval < 0.002) interval = 0.002;  /* Cap at 500 pps */
        
        char interval_str[32];
        snprintf(interval_str, sizeof(interval_str), "%.3f", interval);
        
        /* Log the exact command being used */
#ifdef __APPLE__
        logger_info("Traffic command: ping -i %s %s", interval_str, target);
#else
        logger_info("Traffic command: ping -i %s -n %s", interval_str, target);
#endif
        
        traffic_pid = fork();
        if (traffic_pid < 0) {
            logger_error("Failed to fork traffic generator: %s", strerror(errno));
            return -1;
        }
        
        if (traffic_pid == 0) {
            /* Child process - run ping */
            /* Redirect stdout/stderr to /dev/null */
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
            
#ifdef __APPLE__
            /* macOS: ping -i <interval> <target> */
            execlp("ping", "ping", "-i", interval_str, target, (char *)NULL);
#else
            /* Linux: ping -i <interval> -n <target> */
            execlp("ping", "ping", "-i", interval_str, "-n", target, (char *)NULL);
#endif
            /* If exec fails */
            _exit(1);
        }
        
        logger_info("Started ICMP traffic generator (pid=%d, target=%s, rate=%d pps, interval=%.3fs)", 
                    traffic_pid, target, traffic_rate, interval);
        return 0;
    }
    
    logger_warn("Unknown traffic mode: %s", traffic_mode);
    return -1;
}

/**
 * @brief Stop background traffic generator
 */
static void traffic_generator_stop(void) {
    if (traffic_pid > 0) {
        logger_info("Stopping traffic generator (pid=%d)...", traffic_pid);
        
        /* Send SIGINT first (graceful) then SIGKILL as fallback */
        kill(traffic_pid, SIGINT);
        
        /* Wait briefly for graceful shutdown */
        usleep(200000);  /* 200ms */
        
        /* Check if still running and force kill if needed */
        int status;
        pid_t result = waitpid(traffic_pid, &status, WNOHANG);
        if (result == 0) {
            /* Still running, try SIGTERM */
            kill(traffic_pid, SIGTERM);
            usleep(100000);  /* 100ms */
            result = waitpid(traffic_pid, &status, WNOHANG);
            if (result == 0) {
                /* Still running, force kill */
                kill(traffic_pid, SIGKILL);
                waitpid(traffic_pid, &status, 0);
            }
        }
        
        traffic_pid = -1;
        logger_info("Traffic generator stopped");
    }
}

void signal_handler(int signum) {
    logger_info("Signal %d received, shutting down...", signum);
    is_running = 0;
}

/* Comparison function for qsort (doubles) */
static int compare_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* Comparison function for qsort (uint64_t) */
static int compare_uint64(const void *a, const void *b) {
    uint64_t ua = *(const uint64_t *)a;
    uint64_t ub = *(const uint64_t *)b;
    if (ua < ub) return -1;
    if (ua > ub) return 1;
    return 0;
}

/* Compute median of sorted array */
static double median_double(double *arr, int n) {
    if (n <= 0) return 0.0;
    qsort(arr, n, sizeof(double), compare_double);
    if (n % 2 == 1) {
        return arr[n / 2];
    } else {
        return (arr[n / 2 - 1] + arr[n / 2]) / 2.0;
    }
}

static uint64_t median_uint64(uint64_t *arr, int n) {
    if (n <= 0) return 0;
    qsort(arr, n, sizeof(uint64_t), compare_uint64);
    if (n % 2 == 1) {
        return arr[n / 2];
    } else {
        return (arr[n / 2 - 1] + arr[n / 2]) / 2;
    }
}

/* Per-run metrics storage */
typedef struct {
    double pps;
    double mbps;
    uint64_t p95_ns;
    uint64_t pkts_processed;
    uint64_t bytes_processed;
    double capture_elapsed_sec;
    int pps_regressed;              /* 1 if this run shows PPS regression */
    int mbps_regressed;             /* 1 if this run shows Mbps regression */
} run_metrics_t;

void print_usage(const char *program_name) {
    fprintf(stdout, "Usage: %s [OPTIONS]\n", program_name);
    fprintf(stdout, "Options:\n");
#ifdef __APPLE__
    fprintf(stdout, "  -i INTERFACE         Network interface to monitor (default: en0)\n");
#else
    fprintf(stdout, "  -i INTERFACE         Network interface to monitor (default: eth0)\n");
#endif
    fprintf(stdout, "  -d SECONDS           Capture duration in seconds (default: 20, 0=unlimited)\n");
    fprintf(stdout, "  --warmup-sec SEC     Warmup period before measuring (default: 2, 0=off)\n");
    fprintf(stdout, "  --measure-sec SEC    Measurement period after warmup (default: duration)\n");
    fprintf(stdout, "  --runs N             Number of measurement runs (default: 5)\n");
    fprintf(stdout, "  -n COUNT             Number of packets to capture (default: unlimited)\n");
    fprintf(stdout, "  -t THREADS           Number of processing threads (default: 4)\n");
    fprintf(stdout, "  --icmp               Filter to capture ICMP/ICMPv6 packets only\n");
    fprintf(stdout, "  --stats-interval SEC Print live metrics every SEC seconds (default: 1, 0=off)\n");
    fprintf(stdout, "  --debug              Enable debug logging\n");
    fprintf(stdout, "  --metrics-interval-ms N  Print metrics every N milliseconds\n");
    fprintf(stdout, "  --metrics-json FILE  Write final JSON metrics to FILE on exit\n");
    fprintf(stdout, "  --min-packets N      Minimum packets for valid run (default: 200)\n");
    fprintf(stdout, "\nTraffic Generation:\n");
    fprintf(stdout, "  --traffic MODE       Generate background traffic during warmup+measurement\n");
    fprintf(stdout, "                       Modes: icmp (runs ping)\n");
    fprintf(stdout, "  --traffic-rate N     Traffic rate in packets/sec (default: 50, max: 500)\n");
    fprintf(stdout, "  --traffic-target IP  Target IP for traffic generation (default: 8.8.8.8)\n");
    fprintf(stdout, "\nRegression Detection:\n");
    fprintf(stdout, "  --baseline FILE      Load baseline metrics from JSON file\n");
    fprintf(stdout, "  --fail-on-regression Exit with code 2 if regression detected\n");
    fprintf(stdout, "  --regression-threshold F  Threshold for regression (default: 0.10 = 10%%)\n");
    fprintf(stdout, "\nExit Codes:\n");
    fprintf(stdout, "  0  Success\n");
    fprintf(stdout, "  2  Performance regression detected (with --fail-on-regression)\n");
    fprintf(stdout, "  3  Insufficient sample (packets < --min-packets)\n");
    fprintf(stdout, "  4  Baseline config mismatch (with --fail-on-regression)\n");
    fprintf(stdout, "\n  -h, --help           Print this help message\n");
    fprintf(stdout, "\nExamples:\n");
#ifdef __APPLE__
    fprintf(stdout, "  # Capture packets for 30 seconds:\n");
    fprintf(stdout, "  sudo %s -i en0 -d 30\n", program_name);
#else
    fprintf(stdout, "  # Capture packets for 30 seconds:\n");
    fprintf(stdout, "  sudo %s -i eth0 -d 30\n", program_name);
#endif
    fprintf(stdout, "\n  # Create baseline:\n");
#ifdef __APPLE__
    fprintf(stdout, "  sudo %s -i en0 -d 60 --metrics-json baseline.json\n", program_name);
#else
    fprintf(stdout, "  sudo %s -i eth0 -d 60 --metrics-json baseline.json\n", program_name);
#endif
    fprintf(stdout, "\n  # Run with regression check:\n");
#ifdef __APPLE__
    fprintf(stdout, "  sudo %s -i en0 -d 60 --baseline baseline.json --fail-on-regression\n", program_name);
#else
    fprintf(stdout, "  sudo %s -i eth0 -d 60 --baseline baseline.json --fail-on-regression\n", program_name);
#endif
}

int main(int argc, char *argv[]) {
#ifdef __APPLE__
    char interface_name[256] = "en0";
#else
    char interface_name[256] = "eth0";
#endif
    int num_threads = NUM_THREADS;
    uint32_t max_packets = PACKETS_TO_CAPTURE;
    log_level_t log_level = LOG_INFO;

    /* Long options for getopt_long */
    static struct option long_options[] = {
        {"icmp",                no_argument,       0, 'I'},
        {"warmup-sec",          required_argument, 0, 'W'},
        {"measure-sec",         required_argument, 0, 'E'},
        {"runs",                required_argument, 0, 'N'},
        {"stats-interval",      required_argument, 0, 'S'},
        {"min-packets",         required_argument, 0, 'P'},
        {"traffic",             required_argument, 0, 'T'},
        {"traffic-rate",        required_argument, 0, 'A'},
        {"traffic-target",      required_argument, 0, 'G'},
        {"debug",               no_argument,       0, 'D'},
        {"metrics-interval-ms", required_argument, 0, 'M'},
        {"metrics-json",        required_argument, 0, 'J'},
        {"baseline",            required_argument, 0, 'B'},
        {"fail-on-regression",  no_argument,       0, 'F'},
        {"regression-threshold", required_argument, 0, 'R'},
        {"help",                no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    /* Parse command line arguments */
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "i:d:n:t:hDI", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'i':
                strncpy(interface_name, optarg, sizeof(interface_name) - 1);
                interface_name[sizeof(interface_name) - 1] = '\0';
                break;
            case 'd':
                duration_sec = atoi(optarg);
                break;
            case 'n':
                max_packets = atoi(optarg);
                break;
            case 't':
                num_threads = atoi(optarg);
                break;
            case 'I':
                filter_icmp = 1;
                break;
            case 'W':
                warmup_sec = atoi(optarg);
                break;
            case 'E':
                measure_sec = atoi(optarg);
                break;
            case 'N':
                num_runs = atoi(optarg);
                if (num_runs < 1) num_runs = 1;
                break;
            case 'S':
                stats_interval_sec = atoi(optarg);
                break;
            case 'P':
                min_packets = (uint32_t)atoi(optarg);
                break;
            case 'T':
                traffic_mode = strdup(optarg);
                break;
            case 'A':
                traffic_rate = atoi(optarg);
                if (traffic_rate < 1) traffic_rate = 1;
                if (traffic_rate > 500) traffic_rate = 500;  /* Cap at 500 pps */
                break;
            case 'G':
                traffic_target = strdup(optarg);
                break;
            case 'D':
                log_level = LOG_DEBUG;
                break;
            case 'M':
                metrics_interval_ms = atoi(optarg);
                break;
            case 'J':
                metrics_json_path = strdup(optarg);
                break;
            case 'B':
                baseline_path = strdup(optarg);
                break;
            case 'F':
                fail_on_regression = 1;
                break;
            case 'R':
                regression_threshold = strtod(optarg, NULL);
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }

    /* Initialize logger */
    logger_init(NULL, log_level);
    logger_info("=== Network Packet Analyzer Started ===");
    logger_info("Capturing on interface: %s", interface_name);
    logger_info("Threads: %d, Max Packets: %s",
                num_threads, max_packets ? "limited" : "unlimited");
    
    /* Determine measurement period: use measure_sec if set, otherwise duration_sec */
    int actual_measure_sec = (measure_sec > 0) ? measure_sec : duration_sec;
    int total_duration_sec = warmup_sec + actual_measure_sec;
    
    if (total_duration_sec > 0) {
        logger_info("Duration: %d seconds total (warmup: %d, measure: %d)", 
                    total_duration_sec, warmup_sec, actual_measure_sec);
    } else {
        logger_info("Duration: unlimited (use Ctrl+C to stop)");
    }
    if (num_runs > 1) {
        logger_info("Runs: %d (using median for aggregation)", num_runs);
    }
    if (filter_icmp) {
        logger_info("Filter: ICMP/ICMPv6 only");
    }
    if (stats_interval_sec > 0) {
        logger_info("Stats interval: %d seconds", stats_interval_sec);
    }
    if (metrics_interval_ms > 0) {
        logger_info("Metrics interval: %d ms", metrics_interval_ms);
    }
    if (metrics_json_path != NULL) {
        logger_info("Metrics JSON output: %s", metrics_json_path);
    }
    if (baseline_path != NULL) {
        logger_info("Baseline file: %s", baseline_path);
        logger_info("Regression threshold: %.1f%%", regression_threshold * 100);
        if (fail_on_regression) {
            logger_info("Fail on regression: ENABLED (exit code %d)", EXIT_REGRESSION);
        }
    }

    /* Initialize metrics */
    metrics_init();

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

    /* Apply packet filter if requested */
    if (filter_icmp) {
        if (socket_set_filter(socket_config, FILTER_ICMP) < 0) {
            logger_critical("Failed to attach ICMP filter");
            socket_cleanup(socket_config);
            return 1;
        }
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

    /* Allocate per-run metrics storage */
    run_metrics_t *run_results = NULL;
    if (num_runs > 1) {
        run_results = (run_metrics_t *)calloc(num_runs, sizeof(run_metrics_t));
        if (run_results == NULL) {
            logger_critical("Failed to allocate run results storage");
            free(packet_buffer);
            thread_pool_destroy(thread_pool);
            socket_cleanup(socket_config);
            return 1;
        }
    }

    /* Set metadata for baseline compatibility tracking */
    metrics_set_metadata(
        interface_name,
        filter_icmp ? "icmp" : "none",
        num_threads,
        socket_config->bpf_buffer_size,  /* Actual BPF buffer size from socket init */
        duration_sec,
        warmup_sec,
        traffic_mode,
        traffic_target ? traffic_target : "8.8.8.8",
        traffic_mode ? traffic_rate : 0
    );

    /* Run measurement loop N times */
    for (int run_idx = 0; run_idx < num_runs && is_running; run_idx++) {
        if (num_runs > 1) {
            logger_info("=== Run %d of %d ===", run_idx + 1, num_runs);
        }
        
        /* Reset metrics for this run */
        metrics_init();
        packets_captured = 0;

        /* Calculate timing for warmup and measurement phases */
        uint64_t loop_start_ns = metrics_now_ns();
        uint64_t warmup_end_ns = (warmup_sec > 0) ? 
            loop_start_ns + (uint64_t)warmup_sec * 1000000000ULL : loop_start_ns;
        uint64_t measure_end_ns = (actual_measure_sec > 0) ?
            warmup_end_ns + (uint64_t)actual_measure_sec * 1000000000ULL : 0;
        
        int warmup_complete = (warmup_sec <= 0);  /* Skip warmup if 0 */
        uint64_t last_metrics_print_ns = loop_start_ns;
        uint64_t last_stats_print_ns = loop_start_ns;
        int run_is_running = 1;

        /* Main packet capture loop */
        /* Start traffic generator at warmup start (runs through entire warmup+measurement) */
        if (traffic_mode != NULL) {
            traffic_generator_start();
        }
        
        if (warmup_sec > 0) {
            logger_info("Starting warmup phase (%d seconds)...", warmup_sec);
        } else {
            logger_info("Starting packet capture...");
            /* Start metrics timing immediately if no warmup */
            metrics_start();
        }
        
        while (is_running && run_is_running) {
            uint64_t now_ns = metrics_now_ns();
            
            /* Check for warmup completion */
            if (!warmup_complete && now_ns >= warmup_end_ns) {
                warmup_complete = 1;
                logger_info("Warmup complete. Starting measurement phase (%d seconds)...", actual_measure_sec);
                /* Reset metrics and start measurement */
                /* Note: Traffic generator already running since warmup start */
                metrics_init();
                metrics_start();
                last_metrics_print_ns = now_ns;
                last_stats_print_ns = now_ns;
            }
            
            /* Check measurement end (only after warmup) */
            if (warmup_complete && measure_end_ns > 0 && now_ns >= measure_end_ns) {
                logger_info("Measurement period complete (%d seconds)", actual_measure_sec);
                /* Stop traffic generator */
                traffic_generator_stop();
                run_is_running = 0;
                break;
            }

            /* Print metrics at interval (only during measurement phase) */
            if (warmup_complete && metrics_interval_ms > 0) {
                uint64_t interval_ns = (uint64_t)metrics_interval_ms * 1000000ULL;
                if (now_ns - last_metrics_print_ns >= interval_ns) {
                    metrics_print_human();
                    last_metrics_print_ns = now_ns;
                }
            }

            /* Print live stats at interval (--stats-interval) - only during measurement */
            if (warmup_complete && stats_interval_sec > 0) {
                uint64_t interval_ns = (uint64_t)stats_interval_sec * 1000000000ULL;
                if (now_ns - last_stats_print_ns >= interval_ns) {
                    metrics_print_live_stats();
                    last_stats_print_ns = now_ns;
                }
            }

            int packet_size = socket_receive_packet(socket_config, packet_buffer, MAX_PACKET_SIZE);
            
            if (packet_size < 0) {
                if (is_running) {
                    logger_error("Error receiving packet");
                }
                continue;
            }
            
            if (packet_size == 0) {
                /* No packet available - sleep briefly to avoid busy-spin */
                usleep(1000);  /* 1ms */
                continue;
            }

            packets_captured++;
            
            /* Record capture metrics (only during measurement phase) */
            if (warmup_complete) {
                metrics_inc_captured((uint32_t)packet_size);
            }

            /* Create and enqueue packet */
            packet_t *packet = packet_create(packet_buffer, packet_size);
            if (packet != NULL) {
                if (thread_pool_enqueue(thread_pool, packet) < 0) {
                    if (warmup_complete) {
                        logger_warn("Failed to enqueue packet (queue full)");
                    }
                    packet_free(packet);
                    /* Note: queue_drops already incremented in thread_pool_enqueue */
                } else {
                    logger_debug("Packet #%u enqueued (size: %d bytes)", packets_captured, packet_size);
                }
            }

            /* Check if we've reached the limit */
            if (max_packets > 0 && packets_captured >= max_packets) {
                logger_info("Reached packet capture limit (%u packets)", max_packets);
                traffic_generator_stop();
                run_is_running = 0;
            }
        }

        /* Ensure traffic generator is stopped (in case of signal interrupt) */
        traffic_generator_stop();

        /* Mark end of capture loop (for accurate throughput calculation) */
        metrics_stop_capture();

        /* Give threads time to process remaining packets */
        logger_info("Waiting for thread pool to finish processing (run %d)...", run_idx + 1);
        usleep(500000); /* 500ms */

        /* Take snapshot and store run results */
        metrics_snapshot_t run_snapshot;
        metrics_snapshot(&run_snapshot);
        
        /* Compute pps and mbps from snapshot */
        double elapsed = run_snapshot.capture_elapsed_sec;
        if (elapsed < 0.001) elapsed = 0.001;  /* Avoid division by zero */
        
        run_results[run_idx].pps = run_snapshot.pkts_processed / elapsed;
        run_results[run_idx].mbps = (run_snapshot.bytes_processed * 8.0) / (elapsed * 1000000.0);
        run_results[run_idx].p95_ns = metrics_percentile_ns(&run_snapshot, 95.0);
        run_results[run_idx].pkts_processed = run_snapshot.pkts_processed;
        run_results[run_idx].bytes_processed = run_snapshot.bytes_processed;
        run_results[run_idx].capture_elapsed_sec = elapsed;

        /* Print run summary */
        logger_info("=== Run %d/%d Results ===", run_idx + 1, num_runs);
        metrics_print_human();

        /* Write per-run JSON if requested */
        if (metrics_json_path != NULL && num_runs > 1) {
            char run_json_path[512];
            /* Insert run number before .json extension */
            const char *dot = strrchr(metrics_json_path, '.');
            if (dot != NULL) {
                size_t base_len = (size_t)(dot - metrics_json_path);
                snprintf(run_json_path, sizeof(run_json_path), "%.*s_run%d%s",
                         (int)base_len, metrics_json_path, run_idx + 1, dot);
            } else {
                snprintf(run_json_path, sizeof(run_json_path), "%s_run%d", metrics_json_path, run_idx + 1);
            }
            if (metrics_snapshot_json(run_json_path) == 0) {
                logger_info("Run %d metrics written to: %s", run_idx + 1, run_json_path);
            }
        }
    }

    /* Compute aggregated metrics using median */
    double pps_values[num_runs];
    double mbps_values[num_runs];
    uint64_t p95_values[num_runs];
    
    for (int i = 0; i < num_runs; i++) {
        pps_values[i] = run_results[i].pps;
        mbps_values[i] = run_results[i].mbps;
        p95_values[i] = run_results[i].p95_ns;
    }
    
    double median_pps = median_double(pps_values, num_runs);
    double median_mbps = median_double(mbps_values, num_runs);
    uint64_t median_p95 = median_uint64(p95_values, num_runs);

    logger_info("=== Aggregated Results (median of %d runs) ===", num_runs);
    logger_info("Median PPS: %.2f", median_pps);
    logger_info("Median Mbps: %.4f", median_mbps);
    logger_info("Median P95 Latency: %lu ns (%.3f ms)", median_p95, median_p95 / 1000000.0);

    /* Check for sufficient sample size */
    uint64_t total_pkts_processed = 0;
    for (int i = 0; i < num_runs; i++) {
        total_pkts_processed += run_results[i].pkts_processed;
    }
    
    int sample_valid = (total_pkts_processed >= min_packets);
    if (!sample_valid) {
        logger_warn("INSUFFICIENT SAMPLE: %llu packets processed < %u minimum required",
                    (unsigned long long)total_pkts_processed, min_packets);
        fprintf(stderr, "\n*** INSUFFICIENT SAMPLE ***\n");
        fprintf(stderr, "Packets processed: %llu (minimum: %u)\n",
                (unsigned long long)total_pkts_processed, min_packets);
        fprintf(stderr, "Run marked as INVALID - no regression comparison will be performed.\n\n");
    }

    /* Write final aggregated JSON if requested */
    if (metrics_json_path != NULL) {
        if (metrics_snapshot_json(metrics_json_path) == 0) {
            logger_info("Final metrics written to: %s", metrics_json_path);
        } else {
            logger_error("Failed to write metrics to: %s", metrics_json_path);
        }
        free(metrics_json_path);
        metrics_json_path = NULL;
    }

    /* Regression comparison using median metrics (only if sample is valid) */
    int exit_code = 0;
    if (!sample_valid) {
        exit_code = EXIT_INSUFFICIENT_SAMPLE;
    } else if (baseline_path != NULL) {
        regression_baseline_t baseline;
        
        if (regression_load_baseline(baseline_path, &baseline) == 0) {
            /* Validate baseline metadata compatibility */
            char compat_error[256] = {0};
            const metrics_metadata_t *current_meta = metrics_get_metadata();
            
            if (!regression_validate_metadata(&baseline, current_meta, compat_error, sizeof(compat_error))) {
                logger_error("Baseline incompatible: %s", compat_error);
                /* Mismatch report already printed by regression_validate_metadata */
                if (fail_on_regression) {
                    exit_code = EXIT_CONFIG_MISMATCH;
                }
            } else {
                /* Check each run for regression and count */
                int pps_regressed_count = 0;
                int mbps_regressed_count = 0;
                
                double baseline_pps = baseline.pkts_processed_per_sec;
                double baseline_mbps = baseline.mbps_processed;
                
                logger_info("=== Per-Run Regression Analysis ===");
                logger_info("Baseline: %.2f pps, %.4f Mbps (threshold: %.1f%%)", 
                            baseline_pps, baseline_mbps, regression_threshold * 100);
                
                for (int i = 0; i < num_runs; i++) {
                    double pps_delta_pct = (run_results[i].pps - baseline_pps) / baseline_pps;
                    double mbps_delta_pct = (run_results[i].mbps - baseline_mbps) / baseline_mbps;
                    
                    int pps_reg = (pps_delta_pct < -regression_threshold) ? 1 : 0;
                    int mbps_reg = (mbps_delta_pct < -regression_threshold) ? 1 : 0;
                    
                    run_results[i].pps_regressed = pps_reg;
                    run_results[i].mbps_regressed = mbps_reg;
                    
                    if (pps_reg) pps_regressed_count++;
                    if (mbps_reg) mbps_regressed_count++;
                    
                    logger_info("  Run %d: %.2f pps (%+.1f%%)%s, %.4f Mbps (%+.1f%%)%s",
                               i + 1,
                               run_results[i].pps, pps_delta_pct * 100, pps_reg ? " [REG]" : "",
                               run_results[i].mbps, mbps_delta_pct * 100, mbps_reg ? " [REG]" : "");
                }
                
                /* Require regression to persist in >= 3/5 runs (or 60% of runs) */
                int min_regressed_runs = (num_runs * 3 + 4) / 5;  /* Ceiling of 60% */
                if (min_regressed_runs < 1) min_regressed_runs = 1;
                
                int pps_persistent = (pps_regressed_count >= min_regressed_runs);
                int mbps_persistent = (mbps_regressed_count >= min_regressed_runs);
                int any_persistent = pps_persistent || mbps_persistent;
                
                /* Also check median values */
                double median_pps_delta = (median_pps - baseline_pps) / baseline_pps;
                double median_mbps_delta = (median_mbps - baseline_mbps) / baseline_mbps;
                int median_pps_reg = (median_pps_delta < -regression_threshold);
                int median_mbps_reg = (median_mbps_delta < -regression_threshold);
                
                logger_info("=== Regression Summary ===");
                logger_info("PPS:  %d/%d runs regressed, median=%.2f (%+.1f%%)%s",
                           pps_regressed_count, num_runs, median_pps, median_pps_delta * 100,
                           median_pps_reg ? " [REG]" : "");
                logger_info("Mbps: %d/%d runs regressed, median=%.4f (%+.1f%%)%s",
                           mbps_regressed_count, num_runs, median_mbps, median_mbps_delta * 100,
                           median_mbps_reg ? " [REG]" : "");
                logger_info("Persistence threshold: %d/%d runs required", min_regressed_runs, num_runs);
                
                fprintf(stdout, "\n================================================================================\n");
                fprintf(stdout, "REGRESSION COMPARISON RESULTS (threshold: %.1f%%)\n", regression_threshold * 100);
                fprintf(stdout, "================================================================================\n");
                fprintf(stdout, "Metric    Baseline      Median        Delta     Runs Regressed  Status\n");
                fprintf(stdout, "--------------------------------------------------------------------------------\n");
                fprintf(stdout, "PPS       %10.2f    %10.2f    %+6.1f%%    %d/%d             %s\n",
                        baseline_pps, median_pps, median_pps_delta * 100,
                        pps_regressed_count, num_runs,
                        pps_persistent ? "REGRESSION" : "OK");
                fprintf(stdout, "Mbps      %10.4f    %10.4f    %+6.1f%%    %d/%d             %s\n",
                        baseline_mbps, median_mbps, median_mbps_delta * 100,
                        mbps_regressed_count, num_runs,
                        mbps_persistent ? "REGRESSION" : "OK");
                fprintf(stdout, "================================================================================\n");
                
                if (any_persistent) {
                    fprintf(stdout, "RESULT: PERFORMANCE REGRESSION DETECTED (persistent across >= %d runs)\n",
                            min_regressed_runs);
                    fprintf(stderr, "\nPERFORMANCE REGRESSION DETECTED\n\n");
                    if (fail_on_regression) {
                        exit_code = EXIT_REGRESSION;
                        logger_warn("Exiting with code %d due to --fail-on-regression", EXIT_REGRESSION);
                    }
                } else {
                    fprintf(stdout, "RESULT: ALL METRICS WITHIN THRESHOLD (or not persistent)\n");
                }
                fprintf(stdout, "================================================================================\n\n");
            }
        } else {
            logger_error("Failed to load baseline file: %s", baseline_path);
        }
        
        free(baseline_path);
        baseline_path = NULL;
    }

    logger_info("Total packets captured: %u", packets_captured);
    logger_info("Total packets processed: %d", thread_pool_get_processed_count(thread_pool));

    /* Cleanup */
    if (run_results != NULL) {
        free(run_results);
    }
    if (traffic_mode != NULL) {
        free(traffic_mode);
        traffic_mode = NULL;
    }
    if (traffic_target != NULL) {
        free(traffic_target);
        traffic_target = NULL;
    }
    free(packet_buffer);
    thread_pool_destroy(thread_pool);
    socket_cleanup(socket_config);

    logger_info("=== Network Packet Analyzer Stopped ===");
    logger_cleanup();

    return exit_code;
}
