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

typedef struct
    tskTaskControlBlock /* The old naming convention is used to prevent breaking kernel aware debuggers. */
{
	volatile StackType_t*
	    pxTopOfStack; /*< Points to the location of the last item placed on the tasks stack.  THIS MUST BE THE FIRST MEMBER OF THE TCB STRUCT. */

#if (portUSING_MPU_WRAPPERS == 1)
	xMPU_SETTINGS
	    xMPUSettings; /*< The MPU settings are defined as part of the port layer.  THIS MUST BE THE SECOND MEMBER OF THE TCB STRUCT. */
#endif

	ListItem_t
	    xStateListItem; /*< The list that the state list item of a task is reference from denotes the state of that task (Ready, Blocked, Suspended ). */
	ListItem_t xEventListItem; /*< Used to reference a task from an event list. */
	UBaseType_t uxPriority; /*< The priority of the task.  0 is the lowest priority. */
	StackType_t* pxStack; /*< Points to the start of the stack. */
	char pcTaskName[configMAX_TASK_NAME_LEN];
	/*< Descriptive name given to the task when created.  Facilitates debugging only. */ /*lint !e971 Unqualified char types are allowed for strings and single characters only. */

#if (configNUMBER_OF_CORES > 1)
	BaseType_t xCoreID; /*< The core that this task is pinned to */
#endif /* configNUMBER_OF_CORES > 1 */

#if ((portSTACK_GROWTH > 0) || (configRECORD_STACK_HIGH_ADDRESS == 1))
	StackType_t* pxEndOfStack; /*< Points to the highest valid address for the stack. */
#endif

#if (portCRITICAL_NESTING_IN_TCB == 1)
	UBaseType_t
	    uxCriticalNesting; /*< Holds the critical section nesting depth for ports that do not maintain their own count in the port layer. */
#endif

#if (configUSE_TRACE_FACILITY == 1)
	UBaseType_t
	    uxTCBNumber; /*< Stores a number that increments each time a TCB is created.  It allows debuggers to determine when a task has been deleted and then recreated. */
	UBaseType_t
	    uxTaskNumber; /*< Stores a number specifically for use by third party trace code. */
#endif

#if (configUSE_MUTEXES == 1)
	UBaseType_t
	    uxBasePriority; /*< The priority last assigned to the task - used by the priority inheritance mechanism. */
	UBaseType_t uxMutexesHeld;
#endif

#if (configUSE_APPLICATION_TASK_TAG == 1)
	TaskHookFunction_t pxTaskTag;
#endif

#if (configNUM_THREAD_LOCAL_STORAGE_POINTERS > 0)
	void* pvThreadLocalStoragePointers[configNUM_THREAD_LOCAL_STORAGE_POINTERS];
#endif

#if (configGENERATE_RUN_TIME_STATS == 1)
	configRUN_TIME_COUNTER_TYPE
	    ulRunTimeCounter; /*< Stores the amount of time the task has spent in the Running state. */
#endif

#if ((configUSE_NEWLIB_REENTRANT == 1) || (configUSE_C_RUNTIME_TLS_SUPPORT == 1))
	configTLS_BLOCK_TYPE
	    xTLSBlock; /*< Memory block used as Thread Local Storage (TLS) Block for the task. */
#endif

#if (configUSE_TASK_NOTIFICATIONS == 1)
	volatile uint32_t ulNotifiedValue[configTASK_NOTIFICATION_ARRAY_ENTRIES];
	volatile uint8_t ucNotifyState[configTASK_NOTIFICATION_ARRAY_ENTRIES];
#endif

/* See the comments in FreeRTOS.h with the definition of
     * tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE. */
#if (tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE != \
     0) /*lint !e731 !e9029 Macro has been consolidated for readability reasons. */
	uint8_t
	    ucStaticallyAllocated; /*< Set to pdTRUE if the task is a statically allocated to ensure no attempt is made to free the memory. */
#endif

#if (INCLUDE_xTaskAbortDelay == 1)
	uint8_t ucDelayAborted;
#endif

#if (configUSE_POSIX_ERRNO == 1)
	int iTaskErrno;
#endif
} tskTCB_Minimal;

/* The old tskTCB name is maintained above then typedefed to the new TCB_t name
 * below to enable the use of older kernel aware debuggers. */
typedef tskTCB_Minimal TCB_t_Minimal;

static void report_thread_stack(TaskHandle_t thread, void* user_data);

/**
 * @brief Initialize stack metrics
 *
 * @return int
 */
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

/**
 * @brief Collect and report stack metrics
 *
 */
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

/**
 * @brief Enable stack metrics for a specific thread
 *
 * @param thread
 * @return int
 */
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

/**
 * @brief Report stack metrics for a specific thread
 *
 * @param thread
 * @param user_data
 */
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
	// TCB_t* pxTCB = (TCB_t*)thread;
	// UBaseType_t total_words = pxTCB->pxEndOfStack - pxTCB->pxStack;
	TCB_t_Minimal* pxTCB = (TCB_t_Minimal*)thread;
	uint32_t stack_size = (uint32_t)pxTCB->pxEndOfStack - (uint32_t)pxTCB->pxStack;
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
		SPOTFLOW_LOG("Failed to report stack used percent metric for %s: %d", thread_label,
			     rc);
	}

	int pct_int = (int)used_percent;
	int pct_frac = (int)(used_percent * 100) % 100;
	SPOTFLOW_DEBUG("Stack: thread=%s, used=%d.%01d%%, free=%zu bytes", thread_label, pct_int,
		       pct_frac, unused_bytes);
}
