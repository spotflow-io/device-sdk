#include <errno.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/dhcpv4.h>
#include "wifi.h"
#include "version_helper.h"
#include <version.h>

LOG_MODULE_REGISTER(spotflow_sample_wifi, LOG_LEVEL_INF);

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

#define NET_EVENT_WIFI_MASK                                                   \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |   \
	 NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT | \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

#define WIFI_SSID CONFIG_NET_WIFI_SSID
#define WIFI_PSK CONFIG_NET_WIFI_PASSWORD
#define WIFI_CONNECT_MAX_RETRIES 5
#define WIFI_CONNECT_RETRY_DELAY_S 2

static struct net_if* sta_iface;
static struct wifi_connect_req_params sta_config;
static struct net_mgmt_event_callback cb;
static struct k_work_delayable wifi_reconnect_work;
static bool wifi_connected;
static bool wifi_connect_in_progress;

static void wifi_event_handler(struct net_mgmt_event_callback* cb,
#if SPOTFLOW_ZEPHYR_VERSION_GE(4, 2)
			       uint64_t mgmt_event,
#else
			       uint32_t mgmt_event,
#endif
			       struct net_if* iface);
static void wifi_reconnect_work_handler(struct k_work* work);
static void schedule_wifi_reconnect(void);

int connect_to_wifi(void)
{
	int retries = 0;
	int ret;

	if (!sta_iface) {
		LOG_ERR("STA: interface not initialized");
		return -EIO;
	}

	if (wifi_connected || wifi_connect_in_progress) {
		return 0;
	}

	wifi_connect_in_progress = true;

	sta_config.ssid = (const uint8_t*)WIFI_SSID;
	sta_config.ssid_length = strlen(WIFI_SSID);
	sta_config.psk = (const uint8_t*)WIFI_PSK;
	sta_config.psk_length = strlen(WIFI_PSK);
	sta_config.security = WIFI_SECURITY_TYPE_PSK;
	sta_config.channel = WIFI_CHANNEL_ANY;
	sta_config.band = WIFI_FREQ_BAND_2_4_GHZ;

	LOG_INF("Connecting to SSID: %s", sta_config.ssid);

	do {
		ret =
		    net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &sta_config, sizeof(sta_config));
		if (ret == 0) {
			break;
		}

		if (retries++ < WIFI_CONNECT_MAX_RETRIES) {
			LOG_WRN("Unable to connect to (%s): %d, retrying", WIFI_SSID, ret);
			k_sleep(K_SECONDS(WIFI_CONNECT_RETRY_DELAY_S));
		} else {
			break;
		}
	} while (true);

	if (ret) {
		wifi_connect_in_progress = false;
		LOG_ERR("Unable to Connect to (%s): %d", WIFI_SSID, ret);
	}

	return ret;
}

int init_wifi(void)
{
	k_work_init_delayable(&wifi_reconnect_work, wifi_reconnect_work_handler);

	net_mgmt_init_event_callback(&cb, wifi_event_handler, NET_EVENT_WIFI_MASK);
	net_mgmt_add_event_callback(&cb);

	/* Get STA interface in AP-STA mode. */
	sta_iface = net_if_get_wifi_sta();

	return 0;
}

static void wifi_reconnect_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);
	(void)connect_to_wifi();
}

static void schedule_wifi_reconnect(void)
{
	(void)k_work_reschedule(&wifi_reconnect_work, K_SECONDS(WIFI_CONNECT_RETRY_DELAY_S));
}

/* To provide backward compatibility for zephyr < 4.2.0 */
static void wifi_event_handler(struct net_mgmt_event_callback* cb,
#if SPOTFLOW_ZEPHYR_VERSION_GE(4, 2)
			       uint64_t mgmt_event,
#else
			       uint32_t mgmt_event,
#endif
			       struct net_if* iface)
{
	ARG_UNUSED(iface);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status* result = (struct wifi_status*)cb->info;
		wifi_connect_in_progress = false;

		if (result->status == 0) {
			wifi_connected = true;
			(void)k_work_cancel_delayable(&wifi_reconnect_work);
			LOG_INF("Connected to %s", WIFI_SSID);
#if defined(CONFIG_NET_DHCPV4)
			net_dhcpv4_start(sta_iface);
#endif
		} else {
			wifi_connected = false;
			LOG_ERR("Failed to connect to %s (status %d)", WIFI_SSID, result->status);
			schedule_wifi_reconnect();
		}
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		struct wifi_status* result = (struct wifi_status*)cb->info;
		wifi_connected = false;
		wifi_connect_in_progress = false;

		if (result->status == 0) {
			LOG_INF("Disconnected from %s", WIFI_SSID);
		} else {
			LOG_ERR("Unexpected disconnect from %s (reason %d)", WIFI_SSID,
				result->status);
		}
		schedule_wifi_reconnect();
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
