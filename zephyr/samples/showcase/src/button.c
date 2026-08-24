#include "showcase.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(showcase_button, LOG_LEVEL_INF);

#define SW0_NODE DT_ALIAS(sw0)
#define LONG_PRESS_DURATION K_SECONDS(3)

#if !DT_NODE_HAS_STATUS_OKAY(SW0_NODE)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static struct gpio_callback button_callback_data;
static struct k_work_delayable long_press_work;
static atomic_t button_down;
static atomic_t long_press_reported;

K_MSGQ_DEFINE(button_actions, sizeof(enum showcase_button_action), 4, 1);

static void queue_action(enum showcase_button_action action)
{
	if (k_msgq_put(&button_actions, &action, K_NO_WAIT) < 0) {
		LOG_WRN("Dropping button action because the queue is full");
	}
}

static void long_press_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);

	if (atomic_get(&button_down) && atomic_cas(&long_press_reported, 0, 1)) {
		queue_action(SHOWCASE_BUTTON_LONG_PRESS);
	}
}

static void button_changed(const struct device* dev, struct gpio_callback* cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	int pressed = gpio_pin_get_dt(&button);

	if (pressed > 0 && atomic_cas(&button_down, 0, 1)) {
		atomic_clear(&long_press_reported);
		(void)k_work_reschedule(&long_press_work, LONG_PRESS_DURATION);
	} else if (pressed == 0 && atomic_cas(&button_down, 1, 0)) {
		(void)k_work_cancel_delayable(&long_press_work);
		if (!atomic_get(&long_press_reported)) {
			queue_action(SHOWCASE_BUTTON_SHORT_PRESS);
		}
	}
}

int showcase_button_init(void)
{
	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Button device %s is not ready", button.port->name);
		return -ENODEV;
	}

	int rc = gpio_pin_configure_dt(&button, GPIO_INPUT);

	if (rc < 0) {
		LOG_ERR("Failed to configure the button: %d", rc);
		return rc;
	}

	k_work_init_delayable(&long_press_work, long_press_work_handler);
	gpio_init_callback(&button_callback_data, button_changed, BIT(button.pin));

	rc = gpio_add_callback(button.port, &button_callback_data);
	if (rc < 0) {
		LOG_ERR("Failed to add the button callback: %d", rc);
		return rc;
	}

	rc = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	if (rc < 0) {
		LOG_ERR("Failed to enable button interrupts: %d", rc);
		return rc;
	}

	LOG_INF("Short press: confirm an unconfirmed OTA image");
	LOG_INF("Long press: abort active OTA, otherwise create a coredump");
	return 0;
}

int showcase_button_get_action(enum showcase_button_action* action, k_timeout_t timeout)
{
	if (action == NULL) {
		return -EINVAL;
	}

	return k_msgq_get(&button_actions, action, timeout);
}
