#include "metrics/system/spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "logging/spotflow_log_backend.h"

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include <inttypes.h>

/*
 ESP_RST_UNKNOWN,    //!< Reset reason can not be determined
    ESP_RST_POWERON,    //!< Reset due to power-on event
    ESP_RST_EXT,        //!< Reset by external pin (not applicable for ESP32)
    ESP_RST_SW,         //!< Software reset via esp_restart
    ESP_RST_PANIC,      //!< Software reset due to exception/panic
    ESP_RST_INT_WDT,    //!< Reset (software or hardware) due to interrupt watchdog
    ESP_RST_TASK_WDT,   //!< Reset due to task watchdog
    ESP_RST_WDT,        //!< Reset due to other watchdogs
    ESP_RST_DEEPSLEEP,  //!< Reset after exiting deep sleep mode
    ESP_RST_BROWNOUT,   //!< Brownout reset (software or hardware)
    ESP_RST_SDIO,       //!< Reset over SDIO
    ESP_RST_USB,        //!< Reset by USB peripheral
    ESP_RST_JTAG,       //!< Reset by JTAG
    ESP_RST_EFUSE,      //!< Reset due to efuse error
    ESP_RST_PWR_GLITCH, //!< Reset due to power glitch detected
    ESP_RST_CPU_LOCKUP, //!< Reset due to CPU lock up (double exception)
*/
static const struct {
    uint32_t flag;
    const char* name;
} reset_cause_map[] = {
    { ESP_RST_EXT, "PIN" },
    { ESP_RST_SW, "SOFTWARE" },
    { ESP_RST_BROWNOUT, "BROWNOUT" },
    { ESP_RST_PANIC, "POR" },
    { ESP_RST_WDT, "WATCHDOG" },
    { ESP_RST_JTAG, "DEBUG" },
    { ESP_RST_SDIO, "SDIO" },
    { ESP_RST_DEEPSLEEP, "LOW_POWER_WAKE" },
    { ESP_RST_TASK_WDT, "TASK_WDT" },
    { ESP_RST_EFUSE, "SECURITY" },
};

#define RESET_CAUSE_COUNT (sizeof(reset_cause_map) / sizeof(reset_cause_map[0]))

static void reset_cause_to_string(uint32_t cause, char* buf, size_t buf_len)
{
    size_t used = 0;
    bool first = true;

    if (buf_len == 0) return;
    buf[0] = '\0';

    if (cause == ESP_RST_UNKNOWN) {
        snprintf(buf, buf_len, "UNKNOWN");
        return;
    }

    for (size_t i = 0; i < RESET_CAUSE_COUNT; i++) {
        if (cause == reset_cause_map[i].flag) {
            int n = snprintf(buf + used, buf_len - used, "%s%s", first ? "" : " | ",
                             reset_cause_map[i].name);
            if (n < 0 || (size_t)n >= buf_len - used) return;
            used += n;
            first = false;
        }
    }
}

void spotflow_report_reboot_reason(void)
{
    uint32_t cause = esp_reset_reason();
    char reset_str[64];

    /* Report as immediate event metric */
    static struct spotflow_metric_int* reset_cause_metric;
    int rc = spotflow_register_metric_int_with_labels(
        SPOTFLOW_METRIC_NAME_BOOT_RESET, SPOTFLOW_AGG_INTERVAL_NONE, 1, 1, &reset_cause_metric);
    if (rc < 0) {
        SPOTFLOW_LOG("Failed to register reset cause metric: %d", rc);
        return;
    }

    reset_cause_to_string(cause, reset_str, sizeof(reset_str));
    struct spotflow_label labels[] = { { .key = "reason", .value = reset_str } };
    rc = spotflow_report_metric_int_with_labels(reset_cause_metric, 1, labels, 1);
    if (rc < 0) {
        SPOTFLOW_LOG("Failed to report reset cause: %d", rc);
        return;
    }

    SPOTFLOW_DEBUG("Reset cause reported: 0x%08x, %s", (unsigned int)cause, reset_str);
}