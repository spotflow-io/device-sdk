#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_registry.h"
#include "metrics/system/spotflow_metrics_system.h"

LOG_MODULE_REGISTER(spotflow_ble_minimal_sample, LOG_LEVEL_INF);

#define SW0_NODE DT_ALIAS(sw0)
#define TELEMETRY_INTERVAL_SECONDS 10

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, { 0 });
static struct gpio_callback button_callback_data;
static struct spotflow_metric_int* counter_metric;
static struct spotflow_metric_int* battery_metric;
static struct spotflow_metric_float* acceleration_metric;

static void button_pressed(const struct device* device, struct gpio_callback* callback,
			   uint32_t pins)
{
	ARG_UNUSED(device);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);
	k_oops();
}

static int initialize_button(void)
{
	if (!gpio_is_ready_dt(&button)) {
		return -ENODEV;
	}

	int rc = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (rc != 0) {
		return rc;
	}
	rc = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc != 0) {
		return rc;
	}

	gpio_init_callback(&button_callback_data, button_pressed, BIT(button.pin));
	return gpio_add_callback(button.port, &button_callback_data);
}

static void initialize_metrics(void)
{
	(void)spotflow_register_metric_int("sample_counter", SPOTFLOW_AGG_INTERVAL_NONE,
					   &counter_metric);
	(void)spotflow_register_metric_int("battery_level_percent", SPOTFLOW_AGG_INTERVAL_NONE,
					   &battery_metric);
	(void)spotflow_register_metric_float("accelerometer_magnitude_g",
					     SPOTFLOW_AGG_INTERVAL_NONE, &acceleration_metric);

	for (int attempt = 0; attempt < 20; ++attempt) {
		int rc = spotflow_metrics_system_enable_thread_stack_with_label(NULL, "main");
		if (rc == 0 || rc == -EEXIST) {
			break;
		}
		k_sleep(K_MSEC(100));
	}
}

int main(void)
{
	int rc = initialize_button();
	if (rc < 0) {
		LOG_ERR("Failed to initialize button: %d", rc);
		return rc;
	}

	initialize_metrics();
	LOG_INF("Starting Spotflow minimal BLE sample");

	uint32_t counter = 0;
	int battery = 100;
	while (true) {
		counter++;
		if (counter_metric != NULL) {
			(void)spotflow_report_metric_int(counter_metric, counter);
		}
		if ((counter % TELEMETRY_INTERVAL_SECONDS) == 0U) {
			if (battery_metric != NULL) {
				(void)spotflow_report_metric_int(battery_metric, battery);
			}
			if (acceleration_metric != NULL) {
				float magnitude = (float)(sys_rand32_get() % 201U) / 100.0f;
				(void)spotflow_report_metric_float(acceleration_metric, magnitude);
			}
			battery = battery == 0 ? 100 : battery - 1;
		}
		LOG_INF("Minimal Spotflow heartbeat: %u", counter);
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
