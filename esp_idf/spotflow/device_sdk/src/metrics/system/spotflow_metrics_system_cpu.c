#include "metrics/system/spotflow_metrics_system_connection.h"
#include "metrics/system/spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"
#include "logging/spotflow_log_backend.h"

#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

static struct spotflow_metric_float* g_cpu_utilization_metric;

/* Track previous idle ticks for CPU utilization estimation */
static uint32_t prev_idle_ticks = 0;
static int64_t prev_time_us = 0;

/* Approximate CPU utilization by measuring idle task high-water mark delta */
static float get_cpu_utilization(void)
{
    UBaseType_t idle_high_water = uxTaskGetStackHighWaterMark(NULL); // current task, can improve with system-wide
    int64_t now_us = esp_timer_get_time();

    if (prev_time_us == 0) {
        prev_time_us = now_us;
        prev_idle_ticks = idle_high_water;
        return 0.0f; // first measurement
    }

    int64_t delta_time = now_us - prev_time_us;
    uint32_t delta_idle = prev_idle_ticks - idle_high_water;

    prev_time_us = now_us;
    prev_idle_ticks = idle_high_water;

    if (delta_time == 0) return 0.0f;

    float utilization = 100.0f * (1.0f - ((float)delta_idle / (float)delta_time));
    if (utilization < 0.0f) utilization = 0.0f;
    if (utilization > 100.0f) utilization = 100.0f;

    return utilization;
}

int spotflow_metrics_system_cpu_init(void)
{
    int rc = spotflow_register_metric_float(
        SPOTFLOW_METRIC_NAME_CPU,
        SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
        &g_cpu_utilization_metric);

    if (rc < 0) {
        SPOTFLOW_LOG("Failed to register CPU utilization metric: %d", rc);
        return rc;
    }

    SPOTFLOW_LOG("Registered CPU utilization metric");
    return 1;
}

void spotflow_metrics_system_cpu_collect(void)
{
    if (!g_cpu_utilization_metric) {
        SPOTFLOW_LOG("CPU metric not registered");
        return;
    }

    float utilization = get_cpu_utilization();

    int rc = spotflow_report_metric_float(g_cpu_utilization_metric, utilization);
    if (rc < 0) {
        SPOTFLOW_LOG("Failed to report CPU utilization: %d", rc);
    }

    SPOTFLOW_DEBUG("CPU utilization: %.1f%%", utilization);
}