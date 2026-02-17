# Quick Reference - Network Packet Analyzer

## Build

```bash
make                  # Build release
make debug            # Build with debug symbols
make clean            # Remove artifacts
make test             # Run all unit tests
make test-regression  # Run regression validation tests
```

## Basic Capture

```bash
# Simple capture (default 20 seconds)
sudo ./build/packet_analyzer -i en0

# ICMP only, 30 seconds
sudo ./build/packet_analyzer -i en0 --icmp -d 30

# With JSON output
sudo ./build/packet_analyzer -i en0 --metrics-json results.json
```

## Deterministic Benchmarking

### Generate Baseline

```bash
sudo ./build/packet_analyzer \
  -i en0 --icmp \
  --traffic icmp --traffic-rate 50 \
  --warmup-sec 2 --measure-sec 10 \
  --runs 5 --min-packets 200 \
  --metrics-json ci/baseline.json
```

### Run Regression Gate

```bash
sudo ./build/packet_analyzer \
  -i en0 --icmp \
  --traffic icmp --traffic-rate 50 \
  --warmup-sec 2 --measure-sec 10 \
  --runs 5 --min-packets 200 \
  --baseline ci/baseline.json \
  --fail-on-regression
```

## View Metadata

```bash
jq '.metadata' ci/baseline.json
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error |
| 2 | Regression detected |
| 3 | Insufficient samples |
| 4 | Config mismatch |

## CLI Options (Key)

| Option | Description |
|--------|-------------|
| -i, --interface | Network interface |
| -d, --duration | Capture duration (seconds) |
| --icmp | Capture ICMP only |
| --traffic <mode> | Traffic gen: none, icmp |
| --traffic-rate <n> | Packets/sec for traffic gen |
| --warmup-sec <n> | Warmup period (not measured) |
| --measure-sec <n> | Measurement period |
| --runs <n> | Number of runs for aggregation |
| --baseline <file> | Baseline JSON for comparison |
| --fail-on-regression | Exit 2 if regression detected |
| --metrics-json <file> | Output metrics to JSON |

---
**Tip**: Use jq to inspect JSON output: jq . results.json
