#include "metrics/system/spotflow_metrics_system_network.h"
#include "metrics/system/spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"
#include "logging/spotflow_log_backend.h"

#include <inttypes.h>
#include <string.h>
#include <esp_netif.h>   // ESP-IDF network interface API

static struct spotflow_metric_int* g_network_tx_metric;
static struct spotflow_metric_int* g_network_rx_metric;

static void report_network_interface_metrics(esp_netif_t* iface, void* user_data);

int spotflow_metrics_system_network_init(void)
{
    int rc;

    rc = spotflow_register_metric_int_with_labels(
        SPOTFLOW_METRIC_NAME_NETWORK_TX,
        SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
        CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK_MAX_INTERFACES,
        1,
        &g_network_tx_metric
    );
    if (rc < 0) {
        SPOTFLOW_LOG("Failed to register network TX metric");
        return rc;
    }

    rc = spotflow_register_metric_int_with_labels(
        SPOTFLOW_METRIC_NAME_NETWORK_RX,
        SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
        CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK_MAX_INTERFACES,
        1,
        &g_network_rx_metric
    );
    if (rc < 0) {
        SPOTFLOW_LOG("Failed to register network RX metric");
        return rc;
    }

    SPOTFLOW_LOG("Registered network metrics");
    return 0;
}

void spotflow_metrics_system_network_collect(void)
{
    if (!g_network_tx_metric || !g_network_rx_metric) {
        SPOTFLOW_LOG("Network metrics not registered");
        return;
    }

    int if_count = 0;

    esp_netif_t* netif = NULL;

    // Iterate over all interfaces
    for (int i = 0; i < esp_netif_get_nr_of_ifs(); i++) {
        netif = esp_netif_next(netif);
        if (netif) {
            report_network_interface_metrics(netif, &if_count);
        }
    }

    if (if_count == 0) {
        SPOTFLOW_DEBUG("No active network interfaces found");
    }
}

static void report_network_interface_metrics(esp_netif_t* iface, void* user_data)
{
    int* if_count = (int*)user_data;

    if (!esp_netif_is_netif_up(iface)) {
        return;
    }

    const char* if_name = esp_netif_get_desc(iface);
    if (!if_name) {
        SPOTFLOW_LOG("Interface has no valid name");
        return;
    }

    esp_netif_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    if (esp_netif_get_stats(iface, &stats) != ESP_OK) {
        SPOTFLOW_LOG("Failed to get stats for interface");
        return;
    }

    uint64_t tx_bytes = stats.tx_bytes;
    uint64_t rx_bytes = stats.rx_bytes;

    int64_t tx_bytes_capped = (tx_bytes > INT64_MAX) ? INT64_MAX : (int64_t)tx_bytes;
    int64_t rx_bytes_capped = (rx_bytes > INT64_MAX) ? INT64_MAX : (int64_t)rx_bytes;

    struct spotflow_label labels[] = {
        { .key = "interface", .value = if_name }
    };

    int rc = spotflow_report_metric_int_with_labels(
        g_network_tx_metric, tx_bytes_capped, labels, 1);
    if (rc < 0) {
        SPOTFLOW_LOG("Failed to report network TX for interface");
    }

    rc = spotflow_report_metric_int_with_labels(
        g_network_rx_metric, rx_bytes_capped, labels, 1);
    if (rc < 0) {
        SPOTFLOW_LOG("Failed to report network RX for interface");
    }

    SPOTFLOW_DEBUG("Network %s: TX=%" PRIu64 " bytes, RX=%" PRIu64 " bytes",
                   if_name, tx_bytes, rx_bytes);

    (*if_count)++;
}