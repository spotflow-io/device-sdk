#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>
#include "version_helper.h"
#include <zephyr/kernel_version.h>
#include <version.h>

LOG_MODULE_REGISTER(spotflow_sample_wifi, LOG_LEVEL_INF);

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

#define NET_EVENT_WIFI_MASK                                                   \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |   \
	 NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT | \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

#define WIFI_SSID CONFIG_NET_WIFI_SSID
#define WIFI_PSK CONFIG_NET_WIFI_PASSWORD

static struct net_if* sta_iface;

static struct wifi_connect_req_params sta_config;

static struct net_mgmt_event_callback cb;

/* To provide backward compatibility for zephyr < 4.2.0 */
static void wifi_event_handler(struct net_mgmt_event_callback* cb,
#if SPOTFLOW_ZEPHYR_VERSION_GE(4, 2)
			       uint64_t mgmt_event,
#else
			       uint32_t mgmt_event,
#endif
			       struct net_if* iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status* result = (struct wifi_status*)cb->info;

		if (result->status == 0) {
			LOG_INF("Connected to %s", WIFI_SSID);
		} else {
			LOG_ERR("Failed to connect to %s (status %d)", WIFI_SSID, result->status);
		}
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		struct wifi_status* result = (struct wifi_status*)cb->info;

		if (result->status == 0) {
			LOG_INF("Disconnected from %s", WIFI_SSID);
		} else {
			LOG_ERR("Unexpected disconnect from %s (reason %d)", WIFI_SSID,
				result->status);
		}
		break;
	}
	case NET_EVENT_WIFI_AP_ENABLE_RESULT: {
		LOG_INF("AP Mode is enabled. Waiting for station to connect");
		break;
	}
	case NET_EVENT_WIFI_AP_DISABLE_RESULT: {
		LOG_INF("AP Mode is disabled.");
		break;
	}
	case NET_EVENT_WIFI_AP_STA_CONNECTED: {
		struct wifi_ap_sta_info* sta_info = (struct wifi_ap_sta_info*)cb->info;

		LOG_INF("station: " MACSTR " joined ", sta_info->mac[0], sta_info->mac[1],
			sta_info->mac[2], sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
		break;
	}
	case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
		struct wifi_ap_sta_info* sta_info = (struct wifi_ap_sta_info*)cb->info;

		LOG_INF("station: " MACSTR " leave ", sta_info->mac[0], sta_info->mac[1],
			sta_info->mac[2], sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
		break;
	}
	default:
		break;
	}
}

int connect_to_wifi(void)
{
	if (!sta_iface) {
		LOG_ERR("STA: interface not initialized");
		return -EIO;
	}

	sta_config.ssid = (const uint8_t*)WIFI_SSID;
	sta_config.ssid_length = strlen(WIFI_SSID);
	sta_config.psk = (const uint8_t*)WIFI_PSK;
	sta_config.psk_length = strlen(WIFI_PSK);
	sta_config.security = WIFI_SECURITY_TYPE_PSK;
	sta_config.channel = WIFI_CHANNEL_ANY;
	sta_config.band = WIFI_FREQ_BAND_2_4_GHZ;

	LOG_INF("Connecting to SSID: %s\n", sta_config.ssid);

	int ret;
	do {
		ret =
		    net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &sta_config, sizeof(sta_config));
		if (ret == -EAGAIN) {
			LOG_INF("Unable to connect to (%s), retrying...", WIFI_SSID);
			k_sleep(K_SECONDS(1));
		} else {
			break;
		}
	} while (true);

	if (ret) {
		LOG_ERR("Unable to Connect to (%s): %d", WIFI_SSID, ret);
	}

	return ret;
}

int init_wifi(void)
{
	net_mgmt_init_event_callback(&cb, wifi_event_handler, NET_EVENT_WIFI_MASK);
	net_mgmt_add_event_callback(&cb);

	/* Get STA interface in AP-STA mode. */
	sta_iface = net_if_get_wifi_sta();

	return 0;
}
