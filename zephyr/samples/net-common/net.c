#include "net.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spotflow_sample_net, LOG_LEVEL_INF);

#ifdef CONFIG_SPOTFLOW_USE_WIFI
#include "wifi.h"
#endif

#ifdef CONFIG_SPOTFLOW_USE_ETH
#include "eth.h"
#endif

void spotflow_sample_net_init(void)
{
#ifdef CONFIG_SPOTFLOW_USE_WIFI
	LOG_INF("Initializing Wi-Fi...");
	init_wifi();
	connect_to_wifi();
#endif

#ifdef CONFIG_SPOTFLOW_USE_ETH
	LOG_INF("Initializing Ethernet...");
	turn_on_dhcp_when_device_is_up();
#endif
}
