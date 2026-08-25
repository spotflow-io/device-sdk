#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_SPOTFLOW_METRICS
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_registry.h"
#endif

#if defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK) && \
	!defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS)
#include "metrics/system/spotflow_metrics_system.h"
#endif

LOG_MODULE_REGISTER(spotflow_ble_sample, LOG_LEVEL_INF);

#define SW0_NODE DT_ALIAS(sw0)
#define BUTTON_POLL_INTERVAL K_MSEC(50)
#define BUTTON_PRESSED_STATE 1
#define TELEMETRY_THREAD_STACK_SIZE 640
#define TELEMETRY_THREAD_PRIORITY 5
#define TELEMETRY_REPORT_INTERVAL K_SECONDS(10)

/* compatibility macro for older Zephyr versions */
#ifndef DT_NODE_HAS_STATUS_OKAY
#define DT_NODE_HAS_STATUS_OKAY(node_id) DT_NODE_HAS_STATUS(node_id, okay)
#endif

#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

#ifndef CONFIG_SOC_CC2340R5
static const char* const long_warning_message =
	"framing-test payload: abcdefghijklmnopqrstuvwxyz 0123456789 "
	"abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 "
	"abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 "
	"abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789";
#else
/* CC2340R5 is configured with a smaller log buffer */
static const char* const long_warning_message =
	"framing-test payload: abcdefghijklmnopqrstuvwxyz 0123456789 ";
#endif

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, { 0 });
static struct spotflow_metric_int* sample_counter_metric;

#ifdef CONFIG_SPOTFLOW_METRICS
static struct spotflow_metric_int* battery_level_metric;
static struct spotflow_metric_float* accelerometer_magnitude_metric;

static void telemetry_thread_entry(void* arg1, void* arg2, void* arg3);

K_THREAD_DEFINE(telemetry_thread, TELEMETRY_THREAD_STACK_SIZE, telemetry_thread_entry, NULL, NULL,
		NULL, TELEMETRY_THREAD_PRIORITY, 0, 0);
#endif

static struct gpio_callback button_cb_data;

#if defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK) && \
	!defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS)
static void enable_thread_stack_metric(void)
{
	for (int attempt = 0; attempt < 20; ++attempt) {
		int rc = spotflow_metrics_system_enable_thread_stack(NULL);
		if (rc == 0 || rc == -EEXIST) {
			return;
		}
		if (rc != -EINVAL) {
			LOG_WRN("Failed to enable main stack metric: %d", rc);
			return;
		}
		k_sleep(K_MSEC(100));
	}

	LOG_WRN("System stack metrics were not ready");
}
#endif

#ifdef CONFIG_SPOTFLOW_METRICS
static void telemetry_thread_entry(void* arg1, void* arg2, void* arg3)
{
	int battery_level_percent = 100;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	int rc = spotflow_register_metric_int("battery_level_percent", SPOTFLOW_AGG_INTERVAL_NONE,
					      &battery_level_metric);
	if (rc < 0) {
		LOG_ERR("Failed to register battery level metric: %d", rc);
	}

	rc = spotflow_register_metric_float("accelerometer_magnitude_g", SPOTFLOW_AGG_INTERVAL_NONE,
					    &accelerometer_magnitude_metric);
	if (rc < 0) {
		LOG_ERR("Failed to register accelerometer magnitude metric: %d", rc);
	}
#if defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK) && \
!defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS)
	enable_thread_stack_metric();
#endif

	while (true) {
		if (battery_level_metric != NULL) {
			rc = spotflow_report_metric_int(battery_level_metric,
							battery_level_percent);
			if (rc < 0) {
				LOG_WRN("Failed to report battery level metric: %d", rc);
			}
		}

		if (accelerometer_magnitude_metric != NULL) {
			double magnitude_g = (double)(sys_rand32_get() % 201U) / 100.0;

			rc = spotflow_report_metric_float(accelerometer_magnitude_metric,
							  magnitude_g);
			if (rc < 0) {
				LOG_WRN("Failed to report accelerometer magnitude metric: %d", rc);
			}
		}

		battery_level_percent =
			battery_level_percent == 0 ? 100 : battery_level_percent - 1;
		k_sleep(TELEMETRY_REPORT_INTERVAL);
	}
}
#endif

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
#ifdef CONFIG_SPOTFLOW_METRICS
	int metric_rc = spotflow_register_metric_int("sample_counter", SPOTFLOW_AGG_INTERVAL_NONE,
						     &sample_counter_metric);
	if (metric_rc < 0) {
		LOG_ERR("Failed to register sample metric: %d", metric_rc);
	} else {
		LOG_INF("Registered sample metric");
	}
#endif
	int rc;

	LOG_INF("Starting Spotflow BLE sample");
	LOG_INF("Press button to trigger a coredump that will be sent over BLE after reboot.");

	rc = prepare_button();
	if (rc != 0) {
		LOG_ERR("Failed to prepare button, exiting");
		return rc;
	}

#if defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK) && \
	!defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS)
	enable_thread_stack_metric();
#endif

	while (true) {
		log_counter++;

#ifdef CONFIG_SPOTFLOW_METRICS
		if (sample_counter_metric != NULL) {
			rc = spotflow_report_metric_int(sample_counter_metric, log_counter);
			if (rc < 0) {
				LOG_WRN("Failed to report sample metric: %d", rc);
			}
		}
#endif

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
