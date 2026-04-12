#include "metrics/system/spotflow_metrics_system_network.h"
#include "metrics/system/spotflow_metrics_system.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_types.h"
#include "logging/spotflow_log_backend.h"
#include "esp_netif.h"
#include "lwip/stats.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"

#include <inttypes.h>
#include <string.h>

static struct spotflow_metric_int* g_network_tx_metric;
static struct spotflow_metric_int* g_network_rx_metric;

/* Per-interface byte counters */
typedef struct {
	struct netif* lwip_netif;
	netif_linkoutput_fn original_linkoutput;
	netif_input_fn original_input;
	uint64_t tx_bytes;
	uint64_t rx_bytes;
	char name[32];
	bool active;
} spotflow_netif_hook_t;

static spotflow_netif_hook_t g_hooks[CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK_MAX_INTERFACES];
static netif_ext_callback_t g_netif_callback;

/**
 * @brief Hook for handling network transmit operations
 *
 * @param netif
 * @param p
 * @return err_t
 */
static err_t hooked_linkoutput(struct netif* netif, struct pbuf* p)
{
	for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK_MAX_INTERFACES; i++) {
		if (g_hooks[i].active && g_hooks[i].lwip_netif == netif) {
			g_hooks[i].tx_bytes += p->tot_len;
			return g_hooks[i].original_linkoutput(netif, p);
		}
	}
	/* Should never reach here, but fall back safely */
	return netif->linkoutput(netif, p);
}

/**
 * @brief Hook for handling network receive operations
 *
 * @param p
 * @param netif
 * @return err_t
 */
static err_t hooked_input(struct pbuf* p, struct netif* netif)
{
	for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK_MAX_INTERFACES; i++) {
		if (g_hooks[i].active && g_hooks[i].lwip_netif == netif) {
			g_hooks[i].rx_bytes += p->tot_len;
			return g_hooks[i].original_input(p, netif);
		}
	}
	return netif->input(p, netif);
}

/**
 * @brief Callback for handling netif extension events
 *
 * @param netif
 * @param reason
 * @param args
 */
static void netif_ext_callback(struct netif* netif, netif_nsc_reason_t reason,
			       const netif_ext_callback_args_t* args)
{
	if (!(reason & LWIP_NSC_NETIF_ADDED)) {
		return;
	}

	/* Only hook WiFi STA (st) and AP (ap) netifs, skip loopback */
	if (netif->name[0] == 'l' && netif->name[1] == 'o') {
		return;
	}

	/* Find a free slot */
	for (int i = 0; i < CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK_MAX_INTERFACES; i++) {
		if (!g_hooks[i].active) {
			g_hooks[i].lwip_netif = netif;
			g_hooks[i].original_linkoutput = netif->linkoutput;
			g_hooks[i].original_input = netif->input;
			g_hooks[i].tx_bytes = 0;
			g_hooks[i].rx_bytes = 0;
			g_hooks[i].active = true;
			snprintf(g_hooks[i].name, sizeof(g_hooks[i].name), "%c%c%d", netif->name[0],
				 netif->name[1], netif->num);

			/* Install hooks */
			netif->linkoutput = hooked_linkoutput;
			netif->input = hooked_input;

			SPOTFLOW_LOG("Installed byte hooks on netif %s", g_hooks[i].name);
			break;
		}
	}
}

/**
 * @brief Initialize network metrics
 *
 * @return int
 */
int spotflow_metrics_system_network_init(void)
{
	int rc;

	memset(g_hooks, 0, sizeof(g_hooks));

	/* Register netif extended callback — fires when any netif is added */
	netif_add_ext_callback(&g_netif_callback, netif_ext_callback);

	rc = spotflow_register_metric_int_with_labels(
	    SPOTFLOW_METRIC_NAME_NETWORK_TX, SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
	    CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK_MAX_INTERFACES, 1, &g_network_tx_metric);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to register network TX metric");
		return rc;
	}

	rc = spotflow_register_metric_int_with_labels(
	    SPOTFLOW_METRIC_NAME_NETWORK_RX, SPOTFLOW_METRICS_SYSTEM_AGG_INTERVAL,
	    CONFIG_SPOTFLOW_METRICS_SYSTEM_NETWORK_MAX_INTERFACES, 1, &g_network_rx_metric);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to register network RX metric");
		return rc;
	}

	SPOTFLOW_LOG("Registered network metrics");
	return 0;
}

/**
 * @brief Collect and report network metrics
 *
 */
void spotflow_metrics_system_network_collect(void)
{
	if (!g_network_tx_metric || !g_network_rx_metric) {
		SPOTFLOW_LOG("Network metrics not registered");
		return;
	}

	// Iterate over all interfaces
	for (int i = 0; i < esp_netif_get_nr_of_ifs(); i++) {
		if (!g_hooks[i].active) {
			continue;
		}
		uint64_t tx = g_hooks[i].tx_bytes;
		uint64_t rx = g_hooks[i].rx_bytes;

		int64_t tx_capped = (tx > INT64_MAX) ? INT64_MAX : (int64_t)tx;
		int64_t rx_capped = (rx > INT64_MAX) ? INT64_MAX : (int64_t)rx;

		struct spotflow_label labels[] = { { .key = "interface",
						     .value = g_hooks[i].name } };

		int rc = spotflow_report_metric_int_with_labels(g_network_tx_metric, tx_capped,
								labels, 1);
		if (rc < 0) {
			SPOTFLOW_LOG("Failed to report TX for %s", g_hooks[i].name);
		}

		rc = spotflow_report_metric_int_with_labels(g_network_rx_metric, rx_capped, labels,
							    1);
		if (rc < 0) {
			SPOTFLOW_LOG("Failed to report RX for %s", g_hooks[i].name);
		}

		SPOTFLOW_DEBUG("Network %s: TX=%" PRIu64 " bytes, RX=%" PRIu64 " bytes",
			       g_hooks[i].name, tx, rx);
	}
}
