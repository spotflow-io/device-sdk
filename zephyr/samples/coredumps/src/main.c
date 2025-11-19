#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "zephyr/debug/coredump.h"

#include "zephyr/drivers/gpio.h"

#ifdef CONFIG_ETH_DRIVER
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#endif

#include <zephyr/device.h>

#ifdef CONFIG_WIFI
#include "../../wifi-common/wifi.h"
#endif

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);

#define SW0_NODE DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, { 0 });
static struct gpio_callback button_cb_data;

static int prepare_button();

#ifdef CONFIG_ETH_DRIVER
static void turn_on_dhcp_when_device_is_up();
#endif

//to use nxp with ethernet, use build with -DOVERLAY_CONFIG="boards/frdm_rw612_eth.conf"y

int main(void)
{
	LOG_INF("Starting Spotflow logging example");
	LOG_INF("Press button to oops the device. "
		"Coredump will be created and sent to Spotflow on reboot.");
	int ret;
	ret = prepare_button();
	if (ret != 0) {
		LOG_ERR("Failed to prepare button, exiting");
		return ret;
	}

	// Wait for the initialization of Wi-Fi device
	k_sleep(K_SECONDS(1));
#ifdef CONFIG_WIFI
	init_wifi();
	connect_to_wifi();
#endif

#ifdef CONFIG_ETH_DRIVER
	turn_on_dhcp_when_device_is_up();
#endif

	int i = 0;
	while (true) {
		LOG_INF("Hello from Zephyr to Spotflow: %d", i++);
		k_sleep(K_SECONDS(2));
	}
}

static void button_pressed(const struct device* dev, struct gpio_callback* cb, uint32_t pins)
{
	LOG_INF("Button pressed. Going to oops.");
	k_oops();
}

static int prepare_button()
{
	int ret;
	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Error: button device %s is not ready", button.port->name);
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d", ret, button.port->name,
			button.pin);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d", ret,
			button.port->name, button.pin);
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

	LOG_DBG("Set up button at %s pin %d", button.port->name, button.pin);
	return 0;
}



#ifdef CONFIG_ETH_DRIVER
static void handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
		    struct net_if *iface) {
	if (mgmt_event == NET_EVENT_IF_UP) {
		LOG_INF("Interface is up -> starting DHCPv4");
		net_dhcpv4_start(iface);
	}
}
static void turn_on_dhcp_when_device_is_up() {
	static struct net_mgmt_event_callback iface_cb;
	net_mgmt_init_event_callback(&iface_cb, handler, NET_EVENT_IF_UP);
	net_mgmt_add_event_callback(&iface_cb);
}
#endif
