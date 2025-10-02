#include <zephyr/bindesc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_stats.h>

#include "../../common/wifi.h"


LOG_MODULE_REGISTER(SPOTF_MAIN, LOG_LEVEL_DBG);

/* Uncomment this function to provide your own device ID in runtime */
/*const char* spotflow_override_device_id()
{
	return "my_nrf7002dk_test";
}*/

static void capture_network_stats()
{
	uint32_t ipv4_recv;
	uint32_t ipv4_sent;
	uint32_t ipv4_drop;
	struct net_if* iface = net_if_get_default();
	if (iface) {
		const struct net_stats_ip *ip = &iface->stats.ipv4;
		ipv4_recv = ip->recv;
		ipv4_sent = ip->sent;
		ipv4_drop = ip->drop;
		LOG_DBG("Network: ipv4_recv=%u ipv4_sent=%u ipv4_drop=%u", ipv4_recv, ipv4_sent,
			ipv4_drop);
	}
}

int main(void)
{
	LOG_INF("Starting Spotflow logging example");

	// Wait for the initialization of Wi-Fi device
	k_sleep(K_SECONDS(1));

	init_wifi();
	connect_to_wifi();

	while (1) {
		capture_network_stats();
		// metrics_capture(&s);
		// metrics_log(&s);
		k_sleep(K_SECONDS(5));
	}
	return 0;
}
