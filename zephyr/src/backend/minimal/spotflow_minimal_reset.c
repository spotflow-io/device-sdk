#include "spotflow_minimal_metrics.h"

#include "metrics/system/spotflow_metrics_system.h"

#include <errno.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_MCUBOOT_IMG_MANAGER)
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/init.h>
#endif

LOG_MODULE_DECLARE(spotflow_metrics, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

static const struct {
	uint32_t flag;
	const char* name;
} g_reset_causes[] = {
	{ RESET_PIN, "PIN" },
	{ RESET_SOFTWARE, "SOFTWARE" },
	{ RESET_BROWNOUT, "BROWNOUT" },
	{ RESET_POR, "POWER_ON" },
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
#ifdef RESET_BOOTLOADER
	{ RESET_BOOTLOADER, "BOOTLOADER" },
#endif
#ifdef RESET_FLASH
	{ RESET_FLASH, "FLASH" },
#endif
};

static struct spotflow_metric_int* g_reset_metric;
static char g_reset_reason[SPOTFLOW_MAX_LABEL_VALUE_LEN];
static bool g_reset_initialized;
#ifdef CONFIG_MCUBOOT_IMG_MANAGER
static bool g_test_upgrade;

static int capture_mcuboot_state(void)
{
	int swap_type = mcuboot_swap_type();
	if (swap_type >= BOOT_SWAP_TYPE_NONE && swap_type <= BOOT_SWAP_TYPE_FAIL) {
		g_test_upgrade = swap_type == BOOT_SWAP_TYPE_REVERT;
	}
	return 0;
}
SYS_INIT(capture_mcuboot_state, APPLICATION, 0);
#endif

static bool append_reason(const char* reason, size_t* used, bool* first)
{
	int written = snprintk(g_reset_reason + *used, sizeof(g_reset_reason) - *used, "%s%s",
			       *first ? "" : " | ", reason);
	if (written < 0 || (size_t)written >= sizeof(g_reset_reason) - *used) {
		g_reset_reason[*used] = '\0';
		return false;
	}
	*used += written;
	*first = false;
	return true;
}

int spotflow_minimal_reset_init(void)
{
	if (g_reset_initialized) {
		return 0;
	}

	uint32_t cause = 0;
	int rc = hwinfo_get_reset_cause(&cause);
	bool supported = rc != -ENOSYS;
	if (rc < 0 && rc != -ENOSYS) {
		return rc;
	}

	rc = spotflow_minimal_register_static_label_int(SPOTFLOW_METRIC_NAME_BOOT_RESET,
							SPOTFLOW_AGG_INTERVAL_NONE, "reason", 1,
							&g_reset_metric);
	if (rc < 0) {
		return rc;
	}

	size_t used = 0;
	bool first = true;
	g_reset_reason[0] = '\0';
#ifdef CONFIG_MCUBOOT_IMG_MANAGER
	bool test_upgrade = g_test_upgrade;
#else
	bool test_upgrade = false;
#endif
	if (cause == 0 && !test_upgrade) {
		(void)append_reason("UNKNOWN", &used, &first);
	} else {
		for (size_t i = 0; i < ARRAY_SIZE(g_reset_causes); ++i) {
			if ((cause & g_reset_causes[i].flag) != 0 &&
			    !append_reason(g_reset_causes[i].name, &used, &first)) {
				break;
			}
		}
		if (test_upgrade) {
			(void)append_reason("TEST_FIRMWARE_UPGRADE", &used, &first);
		}
	}

	rc = spotflow_minimal_report_static_label_int(g_reset_metric, 1, g_reset_reason);
	if (rc < 0) {
		return rc;
	}
	if (supported) {
		(void)hwinfo_clear_reset_cause();
	}
	g_reset_initialized = true;
	return 0;
}

void spotflow_minimal_reset_process(void)
{
	if (!g_reset_initialized) {
		(void)spotflow_minimal_reset_init();
	}
}
