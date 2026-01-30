/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for timeseries management and eviction
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"

/*
 * Note: Testing timeseries eviction is tricky because:
 * 1. Timeseries count is reset after aggregation timer fires
 * 2. We need to simulate the aggregation cycle
 *
 * For now, we test the basic behavior. Full eviction tests would
 * require either:
 * - Exposing internal APIs for testing
 * - Using longer test timeouts to let aggregation timers fire
 * - Mocking the timer system
 */

static struct spotflow_metric_int *g_evict_metric;

static void *timeseries_suite_setup(void)
{
	spotflow_metrics_init();
	return NULL;
}

static void timeseries_before(void *fixture)
{
	ARG_UNUSED(fixture);
	g_evict_metric = NULL;
}

ZTEST_SUITE(timeseries, NULL, timeseries_suite_setup, timeseries_before, NULL, NULL);

/*
 * Test: Timeseries reuse for same labels
 */
ZTEST(timeseries, test_timeseries_reuse)
{
	g_evict_metric = spotflow_register_metric_int_with_labels(
		"reuse_test", SPOTFLOW_AGG_INTERVAL_1MIN, 2, 1);
	zassert_not_null(g_evict_metric, "Failed to register metric");

	struct spotflow_label label = {.key = "id", .value = "same"};

	/* Report multiple values with same label - should reuse same timeseries */
	for (int i = 0; i < 10; i++) {
		int rc = spotflow_report_metric_int_with_labels(g_evict_metric, i * 10, &label, 1);
		zassert_equal(rc, 0, "Failed to report value %d: %d", i, rc);
	}
}

/*
 * Test: Float metric with labels
 */
ZTEST(timeseries, test_float_metric_with_labels)
{
	struct spotflow_metric_float *float_metric = spotflow_register_metric_float_with_labels(
		"float_labeled_test", SPOTFLOW_AGG_INTERVAL_1MIN, 4, 2);
	zassert_not_null(float_metric, "Failed to register float metric");

	struct spotflow_label labels[] = {
		{.key = "sensor", .value = "temp1"},
		{.key = "unit", .value = "celsius"}
	};

	int rc = spotflow_report_metric_float_with_labels(float_metric, 23.5f, labels, 2);
	zassert_equal(rc, 0, "Failed to report float value: %d", rc);

	rc = spotflow_report_metric_float_with_labels(float_metric, 24.1f, labels, 2);
	zassert_equal(rc, 0, "Failed to report second float value: %d", rc);
}

/*
 * Test: Immediate metric (PT0S / no aggregation)
 */
ZTEST(timeseries, test_immediate_metric)
{
	struct spotflow_metric_int *immediate = spotflow_register_metric_int(
		"immediate_test", SPOTFLOW_AGG_INTERVAL_NONE);
	zassert_not_null(immediate, "Failed to register immediate metric");

	/* Immediate metrics should work without aggregation timer */
	for (int i = 0; i < 5; i++) {
		int rc = spotflow_report_metric_int(immediate, i * 100);
		zassert_equal(rc, 0, "Failed to report immediate value %d: %d", i, rc);
	}
}

/*
 * Test: Event reporting
 */
ZTEST(timeseries, test_event_reporting)
{
	struct spotflow_metric_int *event_metric = spotflow_register_metric_int(
		"event_test", SPOTFLOW_AGG_INTERVAL_NONE);
	zassert_not_null(event_metric, "Failed to register event metric");

	int rc = spotflow_report_event(event_metric);
	zassert_equal(rc, 0, "Failed to report event: %d", rc);
}

/*
 * Test: Event with labels
 */
ZTEST(timeseries, test_event_with_labels)
{
	struct spotflow_metric_int *event_metric = spotflow_register_metric_int_with_labels(
		"labeled_event_test", SPOTFLOW_AGG_INTERVAL_NONE, 4, 2);
	zassert_not_null(event_metric, "Failed to register labeled event metric");

	struct spotflow_label labels[] = {
		{.key = "type", .value = "button_press"},
		{.key = "button", .value = "power"}
	};

	int rc = spotflow_report_event_with_labels(event_metric, labels, 2);
	zassert_equal(rc, 0, "Failed to report labeled event: %d", rc);
}

/*
 * Test: Label key too long
 */
ZTEST(timeseries, test_label_key_too_long)
{
	g_evict_metric = spotflow_register_metric_int_with_labels(
		"long_key_test", SPOTFLOW_AGG_INTERVAL_1MIN, 4, 1);
	zassert_not_null(g_evict_metric, "Failed to register metric");

	/* Key longer than SPOTFLOW_MAX_LABEL_KEY_LEN (16) */
	struct spotflow_label label = {
		.key = "this_key_is_way_too_long_for_the_limit",
		.value = "ok"
	};

	int rc = spotflow_report_metric_int_with_labels(g_evict_metric, 1, &label, 1);
	zassert_not_equal(rc, 0, "Should reject overly long label key");
}

/*
 * Test: Label value too long
 */
ZTEST(timeseries, test_label_value_too_long)
{
	g_evict_metric = spotflow_register_metric_int_with_labels(
		"long_value_test", SPOTFLOW_AGG_INTERVAL_1MIN, 4, 1);
	zassert_not_null(g_evict_metric, "Failed to register metric");

	/* Value longer than SPOTFLOW_MAX_LABEL_VALUE_LEN (32) */
	struct spotflow_label label = {
		.key = "status",
		.value = "this_value_is_definitely_way_too_long_for_the_configured_limit"
	};

	int rc = spotflow_report_metric_int_with_labels(g_evict_metric, 1, &label, 1);
	zassert_not_equal(rc, 0, "Should reject overly long label value");
}

/*
 * Test: NULL label key rejected
 */
ZTEST(timeseries, test_null_label_key)
{
	g_evict_metric = spotflow_register_metric_int_with_labels(
		"null_key_test", SPOTFLOW_AGG_INTERVAL_1MIN, 4, 1);
	zassert_not_null(g_evict_metric, "Failed to register metric");

	struct spotflow_label label = {.key = NULL, .value = "test"};

	int rc = spotflow_report_metric_int_with_labels(g_evict_metric, 1, &label, 1);
	zassert_not_equal(rc, 0, "Should reject NULL label key");
}

/*
 * Test: NULL label value rejected
 */
ZTEST(timeseries, test_null_label_value)
{
	g_evict_metric = spotflow_register_metric_int_with_labels(
		"null_value_test", SPOTFLOW_AGG_INTERVAL_1MIN, 4, 1);
	zassert_not_null(g_evict_metric, "Failed to register metric");

	struct spotflow_label label = {.key = "test", .value = NULL};

	int rc = spotflow_report_metric_int_with_labels(g_evict_metric, 1, &label, 1);
	zassert_not_equal(rc, 0, "Should reject NULL label value");
}

/*
 * Test: Using labeled API on label-less metric fails
 */
ZTEST(timeseries, test_labeled_api_on_labelless_metric)
{
	struct spotflow_metric_int *labelless = spotflow_register_metric_int(
		"labelless_api_test", SPOTFLOW_AGG_INTERVAL_1MIN);
	zassert_not_null(labelless, "Failed to register metric");

	struct spotflow_label label = {.key = "test", .value = "value"};

	int rc = spotflow_report_metric_int_with_labels(labelless, 1, &label, 1);
	zassert_equal(rc, -EINVAL, "Should reject labeled API on label-less metric");
}

/*
 * Test: Using label-less API on labeled metric fails
 */
ZTEST(timeseries, test_labelless_api_on_labeled_metric)
{
	struct spotflow_metric_int *labeled = spotflow_register_metric_int_with_labels(
		"labeled_api_test", SPOTFLOW_AGG_INTERVAL_1MIN, 4, 1);
	zassert_not_null(labeled, "Failed to register metric");

	int rc = spotflow_report_metric_int(labeled, 1);
	zassert_equal(rc, -EINVAL, "Should reject label-less API on labeled metric");
}
