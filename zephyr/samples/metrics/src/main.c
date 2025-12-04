#include <zephyr/bindesc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_SPOTFLOW_USE_ETH
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#endif

#ifdef CONFIG_SPOTFLOW_USE_WIFI
#include "../../wifi-common/wifi.h"
#endif
LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);

/* Uncomment this function to provide your own device ID in runtime */
/*const char* spotflow_override_device_id()
{
	return "my_nrf7002dk_test";
}*/

#ifdef CONFIG_SPOTFLOW_USE_ETH
static void turn_on_dhcp_when_device_is_up();
#endif

int spotflow_metrics_init(void);

int main(void)
{
	LOG_INF("Starting Spotflow logging example");

	// Wait for the initialization of Wi-Fi device
	k_sleep(K_SECONDS(1));

#ifdef CONFIG_SPOTFLOW_USE_WIFI
	init_wifi();
	connect_to_wifi();
#endif

#ifdef CONFIG_SPOTFLOW_USE_ETH
	turn_on_dhcp_when_device_is_up();
#endif

	if (spotflow_metrics_init() != 0) {
		LOG_WRN("Metrics init failed");
	}

	for (int i = 0; i < 20; i++) {
		LOG_INF("Hello from Zephyr to Spotflow: %d", i);
		k_sleep(K_SECONDS(2));
	}

	return 0;
}

#ifdef CONFIG_SPOTFLOW_USE_ETH
static void handler(struct net_mgmt_event_callback* cb, uint64_t mgmt_event, struct net_if* iface)
{
	if (mgmt_event == NET_EVENT_IF_UP) {
		LOG_INF("Interface is up -> starting DHCPv4");
		net_dhcpv4_start(iface);
	}
}
static void turn_on_dhcp_when_device_is_up()
{
	static struct net_mgmt_event_callback iface_cb;
	net_mgmt_init_event_callback(&iface_cb, handler, NET_EVENT_IF_UP);
	net_mgmt_add_event_callback(&iface_cb);
}
#endif
