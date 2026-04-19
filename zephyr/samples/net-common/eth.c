#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dhcpv4.h>

LOG_MODULE_REGISTER(spotflow_sample_eth, LOG_LEVEL_INF);

static void handler(struct net_mgmt_event_callback* cb, uint64_t mgmt_event, struct net_if* iface)
{
	if (mgmt_event == NET_EVENT_IF_UP) {
		LOG_INF("Interface is up -> starting DHCPv4");
		net_dhcpv4_start(iface);
	}
}

void turn_on_dhcp_when_device_is_up(void)
{
	static struct net_mgmt_event_callback iface_cb;
	net_mgmt_init_event_callback(&iface_cb, handler, NET_EVENT_IF_UP);
	net_mgmt_add_event_callback(&iface_cb);
}
