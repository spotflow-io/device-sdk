/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for metrics aggregator
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"

/* Test fixture */
static struct spotflow_metric_int *g_test_metric;
static struct spotflow_metric_int *g_labeled_metric;

static void *aggregator_suite_setup(void)
{
	/* Initialize metrics subsystem */
	int rc = spotflow_metrics_init();
	zassert_equal(rc, 0, "Failed to initialize metrics subsystem");
	return NULL;
}

static void aggregator_before(void *fixture)
{
	ARG_UNUSED(fixture);
	g_test_metric = NULL;
	g_labeled_metric = NULL;
}

static void aggregator_after(void *fixture)
{
	ARG_UNUSED(fixture);
	/* Metrics are not unregistered in current implementation */
}

ZTEST_SUITE(aggregator, NULL, aggregator_suite_setup, aggregator_before, aggregator_after, NULL);

/*
 * Test: Register a simple label-less metric
 */
ZTEST(aggregator, test_register_labelless_metric)
{
	g_test_metric = spotflow_register_metric_int("test_counter", SPOTFLOW_AGG_INTERVAL_1MIN);

	zassert_not_null(g_test_metric, "Failed to register metric");
}

/*
 * Test: Register a labeled metric
 */
ZTEST(aggregator, test_register_labeled_metric)
{
	g_labeled_metric = spotflow_register_metric_int_with_labels(
		"test_labeled", SPOTFLOW_AGG_INTERVAL_1MIN, 4, 2);

	zassert_not_null(g_labeled_metric, "Failed to register labeled metric");
}

/*
 * Test: Report value to label-less metric
 */
ZTEST(aggregator, test_report_labelless_value)
{
	g_test_metric = spotflow_register_metric_int("report_test", SPOTFLOW_AGG_INTERVAL_1MIN);
	zassert_not_null(g_test_metric, "Failed to register metric");

	int rc = spotflow_report_metric_int(g_test_metric, 42);
	zassert_equal(rc, 0, "Failed to report value: %d", rc);

	rc = spotflow_report_metric_int(g_test_metric, 58);
	zassert_equal(rc, 0, "Failed to report second value: %d", rc);
}

/*
 * Test: Report value with labels
 */
ZTEST(aggregator, test_report_labeled_value)
{
	g_labeled_metric = spotflow_register_metric_int_with_labels(
		"labeled_report_test", SPOTFLOW_AGG_INTERVAL_1MIN, 4, 2);
	zassert_not_null(g_labeled_metric, "Failed to register labeled metric");

	struct spotflow_label labels[] = {
		{.key = "endpoint", .value = "/api/test"},
		{.key = "method", .value = "GET"}
	};

	int rc = spotflow_report_metric_int_with_labels(g_labeled_metric, 100, labels, 2);
	zassert_equal(rc, 0, "Failed to report labeled value: %d", rc);
}

/*
 * Test: Multiple label combinations create separate timeseries
 */
ZTEST(aggregator, test_multiple_label_combinations)
{
	g_labeled_metric = spotflow_register_metric_int_with_labels(
		"multi_label_test", SPOTFLOW_AGG_INTERVAL_1MIN, 4, 1);
	zassert_not_null(g_labeled_metric, "Failed to register metric");

	struct spotflow_label label_a = {.key = "status", .value = "200"};
	struct spotflow_label label_b = {.key = "status", .value = "404"};
	struct spotflow_label label_c = {.key = "status", .value = "500"};

	int rc = spotflow_report_metric_int_with_labels(g_labeled_metric, 10, &label_a, 1);
	zassert_equal(rc, 0, "Failed to report label_a");

	rc = spotflow_report_metric_int_with_labels(g_labeled_metric, 20, &label_b, 1);
	zassert_equal(rc, 0, "Failed to report label_b");

	rc = spotflow_report_metric_int_with_labels(g_labeled_metric, 30, &label_c, 1);
	zassert_equal(rc, 0, "Failed to report label_c");

	/* Report again to same label - should use existing timeseries */
	rc = spotflow_report_metric_int_with_labels(g_labeled_metric, 15, &label_a, 1);
	zassert_equal(rc, 0, "Failed to report to existing timeseries");
}

/*
 * Test: Timeseries pool exhaustion returns error
 */
ZTEST(aggregator, test_timeseries_pool_exhaustion)
{
	/* Create metric with only 2 timeseries slots */
	g_labeled_metric = spotflow_register_metric_int_with_labels(
		"pool_exhaust_test", SPOTFLOW_AGG_INTERVAL_1MIN, 2, 1);
	zassert_not_null(g_labeled_metric, "Failed to register metric");

	struct spotflow_label label1 = {.key = "id", .value = "1"};
	struct spotflow_label label2 = {.key = "id", .value = "2"};
	struct spotflow_label label3 = {.key = "id", .value = "3"};

	int rc = spotflow_report_metric_int_with_labels(g_labeled_metric, 1, &label1, 1);
	zassert_equal(rc, 0, "Failed to report label1");

	rc = spotflow_report_metric_int_with_labels(g_labeled_metric, 2, &label2, 1);
	zassert_equal(rc, 0, "Failed to report label2");

	/* Third unique label should fail - pool full, and existing ones have count > 0 */
	rc = spotflow_report_metric_int_with_labels(g_labeled_metric, 3, &label3, 1);
	zassert_equal(rc, -ENOSPC, "Expected -ENOSPC when pool is full, got %d", rc);
}

/*
 * Test: Duplicate metric name rejected
 */
ZTEST(aggregator, test_duplicate_name_rejected)
{
	struct spotflow_metric_int *metric1 = spotflow_register_metric_int(
		"duplicate_test", SPOTFLOW_AGG_INTERVAL_1MIN);
	zassert_not_null(metric1, "Failed to register first metric");

	struct spotflow_metric_int *metric2 = spotflow_register_metric_int(
		"duplicate_test", SPOTFLOW_AGG_INTERVAL_1MIN);
	zassert_is_null(metric2, "Should reject duplicate metric name");
}

/*
 * Test: Metric name normalization
 */
ZTEST(aggregator, test_name_normalization)
{
	/* Names with special chars should be normalized */
	struct spotflow_metric_int *metric = spotflow_register_metric_int(
		"My-Metric.Name With Spaces", SPOTFLOW_AGG_INTERVAL_1MIN);
	zassert_not_null(metric, "Failed to register metric with special chars");

	/* Registering the normalized version should fail (duplicate) */
	struct spotflow_metric_int *metric2 = spotflow_register_metric_int(
		"my_metric_name_with_spaces", SPOTFLOW_AGG_INTERVAL_1MIN);
	zassert_is_null(metric2, "Should reject as duplicate after normalization");
}

/*
 * Test: NULL metric handle rejected
 */
ZTEST(aggregator, test_null_metric_rejected)
{
	int rc = spotflow_report_metric_int(NULL, 42);
	zassert_equal(rc, -EINVAL, "Expected -EINVAL for NULL metric");
}

/*
 * Test: Invalid label count rejected
 */
ZTEST(aggregator, test_invalid_label_count)
{
	g_labeled_metric = spotflow_register_metric_int_with_labels(
		"label_count_test", SPOTFLOW_AGG_INTERVAL_1MIN, 4, 2);
	zassert_not_null(g_labeled_metric, "Failed to register metric");

	struct spotflow_label labels[] = {
		{.key = "a", .value = "1"},
		{.key = "b", .value = "2"},
		{.key = "c", .value = "3"}  /* One too many */
	};

	/* Reporting with 3 labels when max is 2 should fail */
	int rc = spotflow_report_metric_int_with_labels(g_labeled_metric, 100, labels, 3);
	zassert_equal(rc, -EINVAL, "Expected -EINVAL for too many labels, got %d", rc);
}
