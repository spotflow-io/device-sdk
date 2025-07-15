#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "wifi.h"
#include "zephyr/debug/coredump.h"

#include "zephyr/drivers/gpio.h"
#include <zephyr/device.h>

#include <zephyr/sys/reboot.h>
#include <zephyr/fatal.h>   /* for k_sys_fatal_error_handler */
#include <zephyr/logging/log_ctrl.h>


LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);

#define SW0_NODE	DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw2 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;
static int prepare_button();
void check_coredump();


int main(void)
{
	LOG_INF("Starting Spotflow logging example");
	int ret;
	ret = prepare_button();
	if (ret != 0) {
		LOG_ERR("Failed to prepare button, exiting");
		return ret;
	}

	// Wait for the initialization of Wi-Fi device
	k_sleep(K_SECONDS(1));

	init_wifi();
	connect_to_wifi();
	check_coredump();
	for (int i = 0; i < 20; i++) {
		LOG_INF("Hello from Zephyr to Spotflow: %d", i);
		k_sleep(K_SECONDS(2));
	}

	return 0;
}

void check_coredump()
{
	int ret;

	/* 1) Do we have a dump in flash? */
	ret = coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL);
	if (ret < 0) {
		LOG_ERR("coredump_query(HAS): %d", ret);
		return;
	}
	if (ret != 1) {
		LOG_INF("No coredump stored, recieved %d", ret);
		return;
	}else {
		LOG_INF("Coredump found successfully");
	}

	/* 2) How big is it? */
	ret = coredump_query(COREDUMP_QUERY_GET_STORED_DUMP_SIZE, NULL);
	if (ret < 0) {
		LOG_ERR("coredump_query(SIZE): %d", ret);
		return;
	}

	LOG_INF("Found coredump of %d	 bytes", ret);

}


void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	LOG_INF("Button pressed. Going to oops in 2 seconds.");
	k_sleep(K_SECONDS(2));
	k_panic();
}


static int prepare_button()
{
	int ret;
	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Error: button device %s is not ready",
		       button.port->name);
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d",
		       ret, button.port->name, button.pin);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d",
			ret, button.port->name, button.pin);
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

	LOG_INF("Set up button at %s pin %d", button.port->name, button.pin);
	return 0;

}

/*to reboot device after panic*/
FUNC_NORETURN void k_sys_fatal_error_handler(unsigned int reason,
				      const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	LOG_ERR("Halting system");
	k_sleep(K_SECONDS(10));
	sys_reboot(SYS_REBOOT_COLD);
	CODE_UNREACHABLE;
}
