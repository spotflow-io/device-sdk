#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdbool.h>
#include <stdint.h>

#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_registry.h"

LOG_MODULE_REGISTER(spotflow_ble_sample, LOG_LEVEL_INF);

#define SW0_NODE DT_ALIAS(sw0)
#define BUTTON_POLL_INTERVAL K_MSEC(50)
#define BUTTON_PRESSED_STATE 1

#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

static const char* const long_warning_message =
	"framing-test payload: abcdefghijklmnopqrstuvwxyz 0123456789 "
	"abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 "
	"abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 "
	"abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789";

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, { 0 });
static struct spotflow_metric_int* sample_counter_metric;

static struct gpio_callback button_cb_data;

static void button_pressed(void)
{
	LOG_INF("Button pressed. Going to oops.");
	k_oops();
}

static void button_callback(const struct device* dev, struct gpio_callback* cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	button_pressed();
}

static int prepare_button(void)
{
	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Error: button device %s is not ready", button.port->name);
		return -EINVAL;
	}

	int rc = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (rc != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d", rc, button.port->name,
			button.pin);
		return rc;
	}
	rc = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d", rc,
			button.port->name, button.pin);
		return rc;
	}

	gpio_init_callback(&button_cb_data, button_callback, BIT(button.pin));
	rc = gpio_add_callback(button.port, &button_cb_data);
	if (rc != 0) {
		LOG_ERR("Error %d: failed to add button callback", rc);
		return rc;
	}

	LOG_INF("Set up interrupt button at %s pin %d", button.port->name, button.pin);
	return 0;
}

int main(void)
{
	uint32_t log_counter = 0;
	int metric_rc = spotflow_register_metric_int("sample_counter", SPOTFLOW_AGG_INTERVAL_NONE,
						     &sample_counter_metric);
	int rc;

	LOG_INF("Starting Spotflow BLE sample");
	LOG_INF("Press button to trigger a coredump that will be sent over BLE after reboot.");

	rc = prepare_button();
	if (rc != 0) {
		LOG_ERR("Failed to prepare button, exiting");
		return rc;
	}

	if (metric_rc < 0) {
		LOG_ERR("Failed to register sample metric: %d", metric_rc);
	} else {
		LOG_INF("Registered sample metric");
	}

	while (true) {
		log_counter++;

		if (sample_counter_metric != NULL) {
			rc = spotflow_report_metric_int(sample_counter_metric, log_counter);
			if (rc < 0) {
				LOG_WRN("Failed to report sample metric: %d", rc);
			}
		}

		if ((log_counter % 10U) == 0U) {
			LOG_WRN("Spotflow BLE framing test %u: %s", log_counter,
				long_warning_message);
		} else {
			LOG_INF("Hello from Zephyr to Spotflow BLE: %u", log_counter);
		}
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
