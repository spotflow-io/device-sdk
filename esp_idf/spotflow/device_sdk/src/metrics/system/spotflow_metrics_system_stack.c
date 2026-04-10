#include "metrics/system/spotflow_metrics_system_stack.h"
#include "metrics/system/spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"
#include "logging/spotflow_log_backend.h"

#include <errno.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static struct spotflow_metric_int* g_stack_free_metric;
static struct spotflow_metric_float* g_stack_used_percent_metric;

#ifndef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS
#define CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS 4 
static TaskHandle_t g_tracked_threads[CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS];
static SemaphoreHandle_t g_tracked_threads_mutex;
static bool g_tracked_threads_initialized;
#endif

static void report_thread_stack(TaskHandle_t thread, void* user_data);

int spotflow_metrics_system_stack_init(void)
{
    int rc;
    uint16_t max_threads = CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS;

    rc = spotflow_register_metric_int_with_labels(SPOTFLOW_METRIC_NAME_STACK_FREE,
                                                  SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
                                                  max_threads, 1, &g_stack_free_metric);
    if (rc < 0) {
        SPOTFLOW_LOG("Failed to register stack free metric: %d", rc);
        return rc;
    }

    rc = spotflow_register_metric_float_with_labels(
        SPOTFLOW_METRIC_NAME_STACK_USED_PERCENT, SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
        max_threads, 1, &g_stack_used_percent_metric);
    if (rc < 0) {
        SPOTFLOW_LOG("Failed to register stack used percent metric: %d", rc);
        return rc;
    }

#ifndef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS
    g_tracked_threads_mutex = xSemaphoreCreateMutex();
    if (g_tracked_threads_mutex == NULL) {
        return -ENOMEM;
    }
    g_tracked_threads_initialized = true;
#endif

    SPOTFLOW_LOG("Registered stack metrics");
    return 2;
}

void spotflow_metrics_system_stack_collect(void)
{
    if (!g_stack_free_metric) {
        SPOTFLOW_LOG("Stack metric not registered");
        return;
    }

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS
    // Enumerate all tasks
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t* task_array = pvPortMalloc(task_count * sizeof(TaskStatus_t));
    if (task_array == NULL) {
        SPOTFLOW_LOG("Failed to allocate memory for task enumeration");
        return;
    }

    task_count = uxTaskGetSystemState(task_array, task_count, NULL);
    for (UBaseType_t i = 0; i < task_count; i++) {
        report_thread_stack(task_array[i].xHandle, NULL);
    }
    vPortFree(task_array);
#else
    if (!g_tracked_threads_initialized) {
        return;
    }

    xSemaphoreTake(g_tracked_threads_mutex, portMAX_DELAY);
    for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS; i++) {
        if (g_tracked_threads[i] != NULL) {
            report_thread_stack(g_tracked_threads[i], NULL);
        }
    }
    xSemaphoreGive(g_tracked_threads_mutex);
#endif
}

int spotflow_metrics_system_stack_enable_thread(TaskHandle_t thread)
{
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS
    (void)thread;
    SPOTFLOW_LOG("Stack tracking is automatic (ALL_THREADS mode)");
    return 0;
#else
    if (thread == NULL) {
        thread = xTaskGetCurrentTaskHandle();
    }

    if (thread == NULL) {
        return -EINVAL;
    }

    if (!g_tracked_threads_initialized) {
        SPOTFLOW_LOG("Stack metrics not initialized");
        return -EINVAL;
    }

    xSemaphoreTake(g_tracked_threads_mutex, portMAX_DELAY);

    for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS; i++) {
        if (g_tracked_threads[i] == thread) {
            xSemaphoreGive(g_tracked_threads_mutex);
            return -EEXIST;
        }
    }

    for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS; i++) {
        if (g_tracked_threads[i] == NULL) {
            g_tracked_threads[i] = thread;
            xSemaphoreGive(g_tracked_threads_mutex);
            SPOTFLOW_LOG("Added thread %p to stack tracking", (void*)thread);
            return 0;
        }
    }

    xSemaphoreGive(g_tracked_threads_mutex);
    SPOTFLOW_LOG("Maximum tracked threads limit reached");
    return -ENOMEM;
#endif
}

static void report_thread_stack(TaskHandle_t thread, void* user_data)
{
    (void)user_data;

    if (thread == NULL) {
        return;
    }

    UBaseType_t unused_bytes = uxTaskGetStackHighWaterMark(thread);
    if (unused_bytes == (UBaseType_t)-1) {
        return;
    }

    TaskStatus_t task_status;
    vTaskGetInfo(thread, &task_status, pdFALSE, eInvalid);
    size_t stack_size = task_status.usStackHighWaterMark + unused_bytes;
    size_t used_bytes = stack_size - unused_bytes;

    char thread_label[32];
    const char* name = pcTaskGetName(thread);
    if (name != NULL && name[0] != '\0') {
        strncpy(thread_label, name, sizeof(thread_label) - 1);
        thread_label[sizeof(thread_label) - 1] = '\0';
    } else {
        snprintf(thread_label, sizeof(thread_label), "%p", (void*)thread);
    }

    struct spotflow_label labels[] = { { .key = "thread", .value = thread_label } };

    int64_t unused_bytes_capped =
        (unused_bytes > INT64_MAX) ? INT64_MAX : (int64_t)unused_bytes;

    int rc = spotflow_report_metric_int_with_labels(g_stack_free_metric, unused_bytes_capped,
                                                    labels, 1);
    if (rc < 0) {
        SPOTFLOW_LOG("Failed to report stack free metric for %s: %d", thread_label, rc);
    }

    float used_percent = (float)used_bytes / (float)stack_size * 100.0f;
    rc = spotflow_report_metric_float_with_labels(g_stack_used_percent_metric, used_percent,
                                                  labels, 1);
    if (rc < 0) {
        SPOTFLOW_LOG("Failed to report stack used percent metric for %s: %d", thread_label, rc);
    }

    int pct_int = (int)used_percent;
    int pct_frac = (int)((used_percent - pct_int) * 10);
    SPOTFLOW_DEBUG("Stack: thread=%s, used=%d.%01d%%, free=%zu bytes", thread_label, pct_int,
                   pct_frac, unused_bytes);
}