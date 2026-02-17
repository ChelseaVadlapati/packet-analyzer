# Network Packet Analyzer

A high-performance, multi-threaded network packet analyzer for macOS (BPF) and Linux (raw sockets). Captures packets in real-time, computes throughput/latency metrics, and provides CI-ready regression detection with deterministic traffic generation.

## Features

- **BPF packet capture** on macOS, raw sockets on Linux
- **Multi-threaded processing** with configurable thread pool
- **Protocol parsing**: Ethernet, IPv4/IPv6, TCP, UDP, ICMP
- **Real-time metrics**: packets/sec, MB/s, latency histograms (p50/p95/p99)
- **Deterministic benchmarking**: warmup phase, multi-run median aggregation
- **Traffic generation**: built-in ICMP ping for reproducible tests
- **Regression detection**: threshold-based comparison against baseline
- **Metadata validation**: ensures baseline/current configs match
- **CI integration**: GitHub Actions workflow with artifact upload

## Build & Run

\`\`\`bash
# Build
make

# Run (requires sudo for packet capture)
sudo ./build/packet_analyzer -i en0

# Run tests
make test
\`\`\`

Output binary: \`./build/packet_analyzer\`

## CLI Options

| Option | Description | Default |
|--------|-------------|---------|
| \`-i INTERFACE\` | Network interface to monitor | \`en0\` (macOS), \`eth0\` (Linux) |
| \`-d SECONDS\` | Capture duration in seconds (0=unlimited) | \`20\` |
| \`--warmup-sec SEC\` | Warmup period before measuring | \`2\` |
| \`--measure-sec SEC\` | Measurement period after warmup | \`duration\` |
| \`--runs N\` | Number of measurement runs | \`5\` |
| \`-n COUNT\` | Max packets to capture (0=unlimited) | \`0\` |
| \`-t THREADS\` | Number of processing threads | \`4\` |
| \`--icmp\` | Filter to capture ICMP/ICMPv6 only | off |
| \`--stats-interval SEC\` | Print live metrics every N seconds (0=off) | \`1\` |
| \`--debug\` | Enable debug logging | off |
| \`--metrics-json FILE\` | Write final JSON metrics to FILE | none |
| \`--min-packets N\` | Minimum packets for valid run | \`200\` |
| \`--traffic MODE\` | Generate background traffic (\`icmp\`) | none |
| \`--traffic-rate N\` | Traffic rate in packets/sec (max: 500) | \`50\` |
| \`--traffic-target IP\` | Target IP for traffic generation | \`8.8.8.8\` |
| \`--baseline FILE\` | Load baseline metrics from JSON | none |
| \`--fail-on-regression\` | Exit with code 2 if regression detected | off |
| \`--regression-threshold F\` | Regression threshold (0.10 = 10%) | \`0.10\` |

## Deterministic Benchmarking (Recommended)

For reproducible CI benchmarks, use deterministic traffic generation with multi-run aggregation:

### Generate Baseline

\`\`\`bash
sudo ./build/packet_analyzer \\
  -i en0 --icmp \\
  --traffic icmp --traffic-target 8.8.8.8 --traffic-rate 50 \\
  --warmup-sec 2 --measure-sec 10 \\
  --min-packets 200 --runs 5 \\
  --metrics-json ci/baseline.json
\`\`\`

### Run Regression Gate

\`\`\`bash
sudo ./build/packet_analyzer \\
  -i en0 --icmp \\
  --traffic icmp --traffic-target 8.8.8.8 --traffic-rate 50 \\
  --warmup-sec 2 --measure-sec 10 \\
  --min-packets 200 --runs 5 \\
  --baseline ci/baseline.json \\
  --fail-on-regression
\`\`\`

**How it works:**
- Runs 5 measurement iterations, takes **median** of pps/mbps
- **Warmup phase** (2s) excluded from measurement
- **Measurement phase** (10s) uses \`capture_elapsed_sec\` for throughput calculation
- Traffic generator spawns \`ping\` at specified rate during warmup+measurement
- Regression requires **3 of 5 runs** to show degradation (persistence check)

## Exit Codes

| Code | Meaning |
|------|---------|
| \`0\` | Success |
| \`2\` | Performance regression detected (with \`--fail-on-regression\`) |
| \`3\` | Insufficient sample (packets < \`--min-packets\`) |
| \`4\` | Baseline metadata mismatch (with \`--fail-on-regression\`) |

## Metadata Validation

Before comparing metrics, baseline and current metadata are validated:

**MUST MATCH** (fail if different):
- \`filter\`, \`threads\`, \`warmup_sec\`, \`duration_sec\`
- \`traffic_mode\`, \`traffic_target\`, \`traffic_rate\`

**WARN ONLY** (log warning, allow comparison):
- \`interface\`, \`os\`, \`bpf_buffer_size\`

## CI Performance Gate

The repository includes \`.github/workflows/perf-regression.yml\` that:
1. Runs on \`macos-latest\` for every push/PR
2. Auto-detects network interface
3. Compares against \`ci/baseline.json\`
4. Uploads artifacts: \`ci/current.json\`, \`ci/perf.log\`

### Update Baseline

\`\`\`bash
sudo ./build/packet_analyzer -i en0 --icmp \\
  --traffic icmp --traffic-rate 50 \\
  --warmup-sec 2 --measure-sec 10 \\
  --runs 5 --min-packets 200 \\
  --metrics-json ci/baseline.json

git add ci/baseline.json && git commit -m "Update baseline" && git push
\`\`\`

## Testing

\`\`\`bash
make test             # Run all tests
make test-basic       # Basic component tests
make test-regression  # Regression validation tests
\`\`\`

## Requirements

- **macOS**: 10.x+ with BPF support
- **Linux**: Kernel 4.0+ with raw socket support
- **Compiler**: GCC 9+ or Clang with C11 support
- **Privileges**: Root/sudo (required for packet capture)

---
**Version**: 2.0.0 | **Updated**: 2026-02-16
