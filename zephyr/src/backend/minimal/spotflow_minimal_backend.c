#include "backend/minimal/spotflow_minimal_backend.h"

#ifdef CONFIG_SPOTFLOW_COREDUMPS
#include "backend/minimal/spotflow_minimal_coredump.h"
#endif
#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
#include "backend/minimal/spotflow_minimal_logging.h"
#endif
#ifdef CONFIG_SPOTFLOW_METRICS
#include "backend/minimal/spotflow_minimal_metrics.h"
#endif

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_SPOTFLOW_COREDUMPS
BUILD_ASSERT(CONFIG_SPOTFLOW_TELEMETRY_CBOR_BUFFER_SIZE >=
		     CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE + 64,
	     "Minimal telemetry workspace must fit a coredump chunk and its CBOR metadata");
#endif
#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
BUILD_ASSERT(CONFIG_SPOTFLOW_TELEMETRY_CBOR_BUFFER_SIZE >= CONFIG_SPOTFLOW_LOG_BUFFER_SIZE + 64,
	     "Minimal telemetry workspace must fit a formatted log and its CBOR metadata");
#endif
#ifdef CONFIG_SPOTFLOW_METRICS
BUILD_ASSERT(CONFIG_SPOTFLOW_TELEMETRY_CBOR_BUFFER_SIZE >= CONFIG_SPOTFLOW_METRICS_MAX_NAME_LENGTH +
			     SPOTFLOW_MAX_LABEL_KEY_LEN + SPOTFLOW_MAX_LABEL_VALUE_LEN + 96,
	     "Minimal telemetry workspace must fit the largest configured metric message");
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM
#define MINIMAL_SYSTEM_METRIC_COUNT                              \
	(2 * IS_ENABLED(CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP) +   \
	 IS_ENABLED(CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU) +        \
	 IS_ENABLED(CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION) + \
	 2 * IS_ENABLED(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK) +  \
	 IS_ENABLED(CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE))
#define MINIMAL_SYSTEM_SERIES_COUNT                                 \
	(2 * IS_ENABLED(CONFIG_SPOTFLOW_METRICS_SYSTEM_HEAP) +      \
	 IS_ENABLED(CONFIG_SPOTFLOW_METRICS_SYSTEM_CPU) +           \
	 IS_ENABLED(CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION) +    \
	 2 * CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_MAX_THREADS *     \
		 IS_ENABLED(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK) + \
	 IS_ENABLED(CONFIG_SPOTFLOW_METRICS_SYSTEM_RESET_CAUSE))
BUILD_ASSERT(CONFIG_SPOTFLOW_METRICS_MAX_REGISTERED >= MINIMAL_SYSTEM_METRIC_COUNT,
	     "Minimal metric registry cannot fit all enabled system metrics");
BUILD_ASSERT(CONFIG_SPOTFLOW_METRICS_MAX_TIMESERIES >= MINIMAL_SYSTEM_SERIES_COUNT,
	     "Minimal time-series pool cannot fit all enabled system metrics");
#endif

enum minimal_source {
	MINIMAL_SOURCE_COREDUMP,
	MINIMAL_SOURCE_METRICS,
	MINIMAL_SOURCE_LOGS,
	MINIMAL_SOURCE_COUNT,
};

static uint8_t workspace[CONFIG_SPOTFLOW_TELEMETRY_CBOR_BUFFER_SIZE];
static enum minimal_source next_source;

void spotflow_minimal_backend_init(void)
{
#ifdef CONFIG_SPOTFLOW_METRICS
	spotflow_minimal_metrics_init();
#endif
}

int spotflow_minimal_backend_process_one(void)
{
	for (size_t i = 0; i < MINIMAL_SOURCE_COUNT; ++i) {
		enum minimal_source source =
			(enum minimal_source)((next_source + i) % MINIMAL_SOURCE_COUNT);
		int rc = 0;

		switch (source) {
#ifdef CONFIG_SPOTFLOW_COREDUMPS
		case MINIMAL_SOURCE_COREDUMP:
			rc = spotflow_minimal_coredump_process(workspace, sizeof(workspace));
			break;
#endif
#ifdef CONFIG_SPOTFLOW_METRICS
		case MINIMAL_SOURCE_METRICS:
			rc = spotflow_minimal_metrics_process(workspace, sizeof(workspace));
			break;
#endif
#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
		case MINIMAL_SOURCE_LOGS:
			rc = spotflow_minimal_log_process(workspace, sizeof(workspace));
			break;
#endif
		default:
			break;
		}

		if (rc != 0) {
			next_source = (enum minimal_source)((source + 1) % MINIMAL_SOURCE_COUNT);
			return rc;
		}
	}

	return 0;
}
