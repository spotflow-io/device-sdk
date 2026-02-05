#include "spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "zephyr/logging/log.h"

#include <zephyr/drivers/hwinfo.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_DECLARE(spotflow_metrics_system, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

static const struct {
	uint32_t flag;
	const char* name;
} reset_cause_map[] = {
	{ RESET_PIN, "PIN" },
	{ RESET_SOFTWARE, "SOFTWARE" },
	{ RESET_BROWNOUT, "BROWNOUT" },
	{ RESET_POR, "POR" },
	{ RESET_WATCHDOG, "WATCHDOG" },
	{ RESET_DEBUG, "DEBUG" },
	{ RESET_SECURITY, "SECURITY" },
	{ RESET_LOW_POWER_WAKE, "LOW_POWER_WAKE" },
	{ RESET_CPU_LOCKUP, "CPU_LOCKUP" },
	{ RESET_PARITY, "PARITY" },
	{ RESET_PLL, "PLL" },
	{ RESET_CLOCK, "CLOCK" },
	{ RESET_HARDWARE, "HARDWARE" },
	{ RESET_USER, "USER" },
	{ RESET_TEMPERATURE, "TEMPERATURE" },
	{ RESET_BOOTLOADER, "BOOTLOADER" },
	{ RESET_FLASH, "FLASH" },
};

#define RESET_CAUSE_COUNT ARRAY_SIZE(reset_cause_map)

void reset_cause_to_string(uint32_t cause, char* buf, size_t buf_len);

void report_reboot_reason(void)
{
	uint32_t cause;
	char reset_str[64];
	int rc = hwinfo_get_reset_cause(&cause);

	if (rc < 0) {
		LOG_WRN("Failed to get reset cause: %d", rc);
		return;
	}

	/* Report as immediate event metric */
	static struct spotflow_metric_int* reset_cause_metric;
	rc = spotflow_register_metric_int_with_labels(
	    SPOTFLOW_METRIC_NAME_BOOT_RESET, SPOTFLOW_AGG_INTERVAL_NONE, 1, 1, &reset_cause_metric);
	if (rc < 0) {
		LOG_ERR("Failed to register reset cause metric: %d", rc);
		return;
	}

	reset_cause_to_string(cause, reset_str, sizeof(reset_str));
	struct spotflow_label labels[] = { { .key = "reason", .value = reset_str } };
	rc = spotflow_report_metric_int_with_labels(reset_cause_metric, 1, labels, 1);

	if (rc < 0) {
		LOG_ERR("Failed to report reset cause: %d", rc);
		return;
	}

	LOG_DBG("Reset cause reported: 0x%08x, %s", cause, reset_str);

	/* Clear reset cause after reporting */
	hwinfo_clear_reset_cause();
}

void reset_cause_to_string(uint32_t cause, char* buf, size_t buf_len)
{
	size_t used = 0;
	bool first = true;

	if (buf_len == 0) {
		return;
	}

	buf[0] = '\0';

	if (cause == 0U) {
		snprintk(buf, buf_len, "UNKNOWN");
		return;
	}

	for (size_t i = 0; i < RESET_CAUSE_COUNT; i++) {
		if (cause & reset_cause_map[i].flag) {
			int n = snprintk(buf + used, buf_len - used, "%s%s", first ? "" : " | ",
					 reset_cause_map[i].name);

			if (n < 0 || (size_t)n >= buf_len - used) {
				return; /* truncated safely */
			}

			used += n;
			first = false;
		}
	}
}
