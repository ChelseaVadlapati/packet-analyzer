/**
 * @file test_regression.c
 * @brief Unit tests for regression metadata validation
 * 
 * Tests that baseline metadata validation correctly identifies
 * MUST-MATCH mismatches vs WARN-ONLY mismatches.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "regression.h"
#include "metrics.h"
#include "logger.h"

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); \
        tests_passed++; \
    } else { \
        printf("  [FAIL] %s\n", msg); \
        tests_failed++; \
    } \
} while(0)

/**
 * @brief Create a reference metadata struct for testing
 */
static void create_reference_metadata(metrics_metadata_t *meta) {
    memset(meta, 0, sizeof(metrics_metadata_t));
    strncpy(meta->interface, "en0", METRICS_META_STRING_LEN);
    strncpy(meta->filter, "icmp", METRICS_META_STRING_LEN);
    strncpy(meta->os, "Darwin", METRICS_META_STRING_LEN);
    strncpy(meta->git_sha, "abc1234", METRICS_META_STRING_LEN);
    strncpy(meta->traffic_mode, "icmp", METRICS_META_STRING_LEN);
    strncpy(meta->traffic_target, "8.8.8.8", METRICS_META_STRING_LEN);
    meta->threads = 4;
    meta->bpf_buffer_size = 131072;
    meta->duration_sec = 20;
    meta->warmup_sec = 2;
    meta->traffic_rate = 50;
    meta->valid = true;
}

/**
 * @brief Test: Matching metadata should pass validation
 */
void test_metadata_match(void) {
    printf("\n=== Test: Matching metadata passes validation ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == true, "Identical metadata should pass validation");
    TEST_ASSERT(strlen(error_msg) == 0, "No error message for matching metadata");
}

/**
 * @brief Test: Filter mismatch (MUST-MATCH) should fail
 */
void test_filter_mismatch(void) {
    printf("\n=== Test: Filter mismatch triggers failure ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    baseline.metadata.filter[0] = '\0';  /* Clear filter */
    strncpy(baseline.metadata.filter, "none", METRICS_META_STRING_LEN);
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    /* current has filter="icmp", baseline has filter="none" */
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == false, "Filter mismatch should fail validation");
    TEST_ASSERT(strlen(error_msg) > 0, "Error message should be set for mismatch");
    TEST_ASSERT(strstr(error_msg, "MUST-MATCH") != NULL, "Error should mention MUST-MATCH");
}

/**
 * @brief Test: Threads mismatch (MUST-MATCH) should fail
 */
void test_threads_mismatch(void) {
    printf("\n=== Test: Threads mismatch triggers failure ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    baseline.metadata.threads = 8;  /* Different thread count */
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    /* current has threads=4, baseline has threads=8 */
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == false, "Threads mismatch should fail validation");
}

/**
 * @brief Test: Warmup sec mismatch (MUST-MATCH) should fail
 */
void test_warmup_mismatch(void) {
    printf("\n=== Test: Warmup sec mismatch triggers failure ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    baseline.metadata.warmup_sec = 5;  /* Different warmup */
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    /* current has warmup_sec=2, baseline has warmup_sec=5 */
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == false, "Warmup mismatch should fail validation");
}

/**
 * @brief Test: Duration sec mismatch (MUST-MATCH) should fail
 */
void test_duration_mismatch(void) {
    printf("\n=== Test: Duration sec mismatch triggers failure ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    baseline.metadata.duration_sec = 60;  /* Different duration */
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    /* current has duration_sec=20, baseline has duration_sec=60 */
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == false, "Duration mismatch should fail validation");
}

/**
 * @brief Test: Traffic mode mismatch (MUST-MATCH) should fail
 */
void test_traffic_mode_mismatch(void) {
    printf("\n=== Test: Traffic mode mismatch triggers failure ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    strncpy(baseline.metadata.traffic_mode, "none", METRICS_META_STRING_LEN);
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    /* current has traffic_mode="icmp", baseline has traffic_mode="none" */
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == false, "Traffic mode mismatch should fail validation");
}

/**
 * @brief Test: Traffic target mismatch (MUST-MATCH) should fail
 */
void test_traffic_target_mismatch(void) {
    printf("\n=== Test: Traffic target mismatch triggers failure ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    strncpy(baseline.metadata.traffic_target, "1.1.1.1", METRICS_META_STRING_LEN);
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    /* current has traffic_target="8.8.8.8", baseline has traffic_target="1.1.1.1" */
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == false, "Traffic target mismatch should fail validation");
}

/**
 * @brief Test: Traffic rate mismatch (MUST-MATCH) should fail
 */
void test_traffic_rate_mismatch(void) {
    printf("\n=== Test: Traffic rate mismatch triggers failure ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    baseline.metadata.traffic_rate = 100;  /* Different rate */
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    /* current has traffic_rate=50, baseline has traffic_rate=100 */
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == false, "Traffic rate mismatch should fail validation");
}

/**
 * @brief Test: Interface mismatch (WARN-ONLY) should PASS
 */
void test_interface_warn_only(void) {
    printf("\n=== Test: Interface mismatch is WARN-ONLY (should pass) ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    strncpy(baseline.metadata.interface, "eth0", METRICS_META_STRING_LEN);
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    /* current has interface="en0", baseline has interface="eth0" */
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == true, "Interface mismatch should only WARN, not fail");
}

/**
 * @brief Test: OS mismatch (WARN-ONLY) should PASS
 */
void test_os_warn_only(void) {
    printf("\n=== Test: OS mismatch is WARN-ONLY (should pass) ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    strncpy(baseline.metadata.os, "Linux", METRICS_META_STRING_LEN);
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    /* current has os="Darwin", baseline has os="Linux" */
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == true, "OS mismatch should only WARN, not fail");
}

/**
 * @brief Test: BPF buffer size mismatch (WARN-ONLY) should PASS
 */
void test_bpf_buffer_warn_only(void) {
    printf("\n=== Test: BPF buffer size mismatch is WARN-ONLY (should pass) ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    baseline.metadata.bpf_buffer_size = 65536;  /* Different size */
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    /* current has bpf_buffer_size=131072, baseline has bpf_buffer_size=65536 */
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == true, "BPF buffer size mismatch should only WARN, not fail");
}

/**
 * @brief Test: Multiple WARN-ONLY mismatches should still PASS
 */
void test_multiple_warn_only(void) {
    printf("\n=== Test: Multiple WARN-ONLY mismatches still pass ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    create_reference_metadata(&baseline.metadata);
    /* Change all warn-only fields */
    strncpy(baseline.metadata.interface, "eth0", METRICS_META_STRING_LEN);
    strncpy(baseline.metadata.os, "Linux", METRICS_META_STRING_LEN);
    baseline.metadata.bpf_buffer_size = 65536;
    strncpy(baseline.metadata.git_sha, "xyz9999", METRICS_META_STRING_LEN);
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == true, "Multiple WARN-ONLY mismatches should still pass");
}

/**
 * @brief Test: Baseline without metadata should pass with warning
 */
void test_no_baseline_metadata(void) {
    printf("\n=== Test: Baseline without metadata passes with warning ===\n");
    
    regression_baseline_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.valid = true;
    baseline.metadata.valid = false;  /* No metadata */
    
    metrics_metadata_t current;
    create_reference_metadata(&current);
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == true, "Missing baseline metadata should pass (backwards compat)");
}

/**
 * @brief Test: Load baseline from fixture file and validate
 */
void test_load_fixture_valid(void) {
    printf("\n=== Test: Load valid baseline fixture ===\n");
    
    regression_baseline_t baseline;
    int load_result = regression_load_baseline("tests/fixtures/baseline_valid.json", &baseline);
    
    TEST_ASSERT(load_result == 0, "Should load valid baseline fixture");
    TEST_ASSERT(baseline.valid == true, "Baseline should be marked valid");
    TEST_ASSERT(baseline.metadata.valid == true, "Metadata should be marked valid");
    TEST_ASSERT(baseline.metadata.threads == 4, "Threads should be 4");
    TEST_ASSERT(strcmp(baseline.metadata.filter, "icmp") == 0, "Filter should be 'icmp'");
    TEST_ASSERT(baseline.metadata.traffic_rate == 50, "Traffic rate should be 50");
}

/**
 * @brief Test: Load fixture with filter mismatch and validate detection
 */
void test_load_fixture_mismatch_filter(void) {
    printf("\n=== Test: Detect filter mismatch from fixture ===\n");
    
    regression_baseline_t baseline;
    int load_result = regression_load_baseline("tests/fixtures/baseline_mismatch_filter.json", &baseline);
    
    TEST_ASSERT(load_result == 0, "Should load mismatch fixture");
    
    /* Create current metadata matching baseline_valid.json */
    metrics_metadata_t current;
    create_reference_metadata(&current);
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == false, "Filter mismatch should be detected from fixture");
    TEST_ASSERT(strcmp(baseline.metadata.filter, "none") == 0, "Baseline filter should be 'none'");
}

/**
 * @brief Test: Load fixture with traffic mismatch and validate detection
 */
void test_load_fixture_mismatch_traffic(void) {
    printf("\n=== Test: Detect traffic mismatch from fixture ===\n");
    
    regression_baseline_t baseline;
    int load_result = regression_load_baseline("tests/fixtures/baseline_mismatch_traffic.json", &baseline);
    
    TEST_ASSERT(load_result == 0, "Should load traffic mismatch fixture");
    
    /* Create current metadata matching baseline_valid.json */
    metrics_metadata_t current;
    create_reference_metadata(&current);
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == false, "Traffic mismatch should be detected from fixture");
    TEST_ASSERT(baseline.metadata.traffic_rate == 100, "Baseline traffic_rate should be 100");
    TEST_ASSERT(strcmp(baseline.metadata.traffic_target, "1.1.1.1") == 0, "Baseline traffic_target should be '1.1.1.1'");
}

/**
 * @brief Test: Load fixture with only WARN-ONLY mismatches (should pass)
 */
void test_load_fixture_warn_only(void) {
    printf("\n=== Test: Warn-only fixture should pass validation ===\n");
    
    regression_baseline_t baseline;
    int load_result = regression_load_baseline("tests/fixtures/baseline_warn_only.json", &baseline);
    
    TEST_ASSERT(load_result == 0, "Should load warn-only fixture");
    
    /* Create current metadata matching baseline_valid.json */
    metrics_metadata_t current;
    create_reference_metadata(&current);
    
    char error_msg[256] = {0};
    bool result = regression_validate_metadata(&baseline, &current, error_msg, sizeof(error_msg));
    
    TEST_ASSERT(result == true, "Warn-only mismatches should pass validation");
}

int main(void) {
    printf("================================================================================\n");
    printf("              REGRESSION METADATA VALIDATION UNIT TESTS\n");
    printf("================================================================================\n");
    
    /* Initialize logger for tests */
    logger_init(NULL, LOG_WARN);  /* Only show warnings and above */
    
    /* Run all tests */
    
    /* Basic validation tests */
    test_metadata_match();
    
    /* MUST-MATCH field tests */
    test_filter_mismatch();
    test_threads_mismatch();
    test_warmup_mismatch();
    test_duration_mismatch();
    test_traffic_mode_mismatch();
    test_traffic_target_mismatch();
    test_traffic_rate_mismatch();
    
    /* WARN-ONLY field tests */
    test_interface_warn_only();
    test_os_warn_only();
    test_bpf_buffer_warn_only();
    test_multiple_warn_only();
    
    /* Edge case tests */
    test_no_baseline_metadata();
    
    /* Fixture-based tests */
    test_load_fixture_valid();
    test_load_fixture_mismatch_filter();
    test_load_fixture_mismatch_traffic();
    test_load_fixture_warn_only();
    
    /* Cleanup */
    logger_cleanup();
    
    /* Print summary */
    printf("\n================================================================================\n");
    printf("                           TEST SUMMARY\n");
    printf("================================================================================\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Total:  %d\n", tests_passed + tests_failed);
    printf("================================================================================\n");
    
    if (tests_failed > 0) {
        printf("\n*** TESTS FAILED ***\n\n");
        return 1;
    }
    
    printf("\n*** ALL TESTS PASSED ***\n\n");
    return 0;
}
