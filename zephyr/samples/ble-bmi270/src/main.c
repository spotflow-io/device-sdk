#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bmi270_config_file.h"

#if defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK) && \
	!defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS)
#include "metrics/system/spotflow_metrics_system.h"
#endif

LOG_MODULE_REGISTER(spotflow_ble_bmi270_sample, LOG_LEVEL_INF);

#define SW0_NODE DT_ALIAS(sw0)
#define BMI270_NODE DT_NODELABEL(bmi270)
#define BMI270_I2C_CONFIG_INIT_PRIORITY 60
#define BMI270_CHIP_ID 0x24
#define BMI270_CONFIG_CHUNK_SIZE 32
#define BMI270_CONFIG_RETRIES 15

#define BMI270_REG_CHIP_ID 0x00
#define BMI270_REG_ACC_DATA 0x0c
#define BMI270_REG_INTERNAL_STATUS 0x21
#define BMI270_REG_ACC_CONF 0x40
#define BMI270_REG_ACC_RANGE 0x41
#define BMI270_REG_GYR_CONF 0x42
#define BMI270_REG_GYR_RANGE 0x43
#define BMI270_REG_INIT_CTRL 0x59
#define BMI270_REG_INIT_ADDR_0 0x5b
#define BMI270_REG_INIT_DATA 0x5e
#define BMI270_REG_PWR_CONF 0x7c
#define BMI270_REG_PWR_CTRL 0x7d
#define BMI270_REG_CMD 0x7e

#define BMI270_CMD_SOFT_RESET 0xb6
#define BMI270_INTERNAL_STATUS_INIT_OK 0x01
#define BMI270_PWR_CTRL_ACC_GYR_ENABLE 0x06
#define BMI270_ACC_CONF_100_HZ 0xa8
#define BMI270_ACC_RANGE_2G 0x00
#define BMI270_GYR_CONF_100_HZ 0xe8
#define BMI270_GYR_RANGE_500_DPS 0x02

BUILD_ASSERT(CONFIG_I2C_INIT_PRIORITY < BMI270_I2C_CONFIG_INIT_PRIORITY,
	     "BMI270 I2C configuration must run after the I2C driver initialization");

static const struct i2c_dt_spec bmi270 = I2C_DT_SPEC_GET(BMI270_NODE);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, { 0 });
static struct gpio_callback button_cb_data;

static int configure_i2c(void)
{
	if (!device_is_ready(bmi270.bus)) {
		return -ENODEV;
	}

	return i2c_configure(bmi270.bus, I2C_MODE_CONTROLLER | I2C_SPEED_SET(I2C_SPEED_STANDARD));
}

SYS_INIT(configure_i2c, POST_KERNEL, BMI270_I2C_CONFIG_INIT_PRIORITY);

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

static int bmi270_read(uint8_t reg, uint8_t* data, size_t length)
{
	return i2c_burst_read_dt(&bmi270, reg, data, length);
}

static int bmi270_write(uint8_t reg, const uint8_t* data, size_t length)
{
	uint8_t buffer[BMI270_CONFIG_CHUNK_SIZE + 1];

	if (length > BMI270_CONFIG_CHUNK_SIZE) {
		return -EINVAL;
	}

	buffer[0] = reg;
	if (length > 0) {
		memcpy(&buffer[1], data, length);
	}

	return i2c_write_dt(&bmi270, buffer, length + 1);
}

static int bmi270_write_byte(uint8_t reg, uint8_t value)
{
	int rc = bmi270_write(reg, &value, 1);

	if (rc == 0) {
		k_usleep(1000);
	}

	return rc;
}

static int bmi270_load_config(void)
{
	for (size_t offset = 0; offset < sizeof(bmi270_config_file_max_fifo);
	     offset += BMI270_CONFIG_CHUNK_SIZE) {
		uint8_t address[] = { (offset / 2) & 0x0f, (offset / 2) >> 4 };
		size_t remaining = sizeof(bmi270_config_file_max_fifo) - offset;
		size_t length = MIN(remaining, BMI270_CONFIG_CHUNK_SIZE);
		int rc = bmi270_write(BMI270_REG_INIT_ADDR_0, address, sizeof(address));

		if (rc != 0) {
			return rc;
		}
		k_usleep(1000);

		rc = bmi270_write(BMI270_REG_INIT_DATA, &bmi270_config_file_max_fifo[offset],
				  length);
		if (rc != 0) {
			return rc;
		}
		k_usleep(1000);
	}

	return 0;
}

static int configure_bmi270(void)
{
	uint8_t value;
	int rc;

	rc = bmi270_read(BMI270_REG_CHIP_ID, &value, 1);
	if (rc != 0) {
		LOG_ERR("BMI270 CHIP_ID read failed: %d", rc);
		return rc;
	}
	if (value != BMI270_CHIP_ID) {
		LOG_ERR("Unexpected BMI270 CHIP_ID: 0x%02x", value);
		return -ENODEV;
	}

	rc = bmi270_write_byte(BMI270_REG_CMD, BMI270_CMD_SOFT_RESET);
	if (rc != 0) {
		return rc;
	}
	k_sleep(K_MSEC(2));

	rc = bmi270_write_byte(BMI270_REG_PWR_CONF, 0x00);
	if (rc != 0) {
		return rc;
	}
	rc = bmi270_write_byte(BMI270_REG_INIT_CTRL, 0x00);
	if (rc != 0) {
		return rc;
	}
	rc = bmi270_load_config();
	if (rc != 0) {
		return rc;
	}
	rc = bmi270_write_byte(BMI270_REG_INIT_CTRL, 0x01);
	if (rc != 0) {
		return rc;
	}

	for (int attempt = 0; attempt <= BMI270_CONFIG_RETRIES; ++attempt) {
		rc = bmi270_read(BMI270_REG_INTERNAL_STATUS, &value, 1);
		if (rc != 0) {
			return rc;
		}
		if ((value & 0x0f) == BMI270_INTERNAL_STATUS_INIT_OK) {
			break;
		}
		if (attempt == BMI270_CONFIG_RETRIES) {
			LOG_ERR("BMI270 configuration failed, internal status: 0x%02x", value);
			return -EIO;
		}
		k_sleep(K_MSEC(10));
	}

	rc = bmi270_write_byte(BMI270_REG_ACC_CONF, BMI270_ACC_CONF_100_HZ);
	if (rc != 0) {
		return rc;
	}
	rc = bmi270_write_byte(BMI270_REG_ACC_RANGE, BMI270_ACC_RANGE_2G);
	if (rc != 0) {
		return rc;
	}
	rc = bmi270_write_byte(BMI270_REG_GYR_CONF, BMI270_GYR_CONF_100_HZ);
	if (rc != 0) {
		return rc;
	}
	rc = bmi270_write_byte(BMI270_REG_GYR_RANGE, BMI270_GYR_RANGE_500_DPS);
	if (rc != 0) {
		return rc;
	}
	rc = bmi270_write_byte(BMI270_REG_PWR_CTRL, BMI270_PWR_CTRL_ACC_GYR_ENABLE);
	if (rc != 0) {
		return rc;
	}
	rc = bmi270_write_byte(BMI270_REG_PWR_CONF, 0x01);
	if (rc != 0) {
		return rc;
	}

	k_sleep(K_MSEC(45));
	rc = bmi270_read(BMI270_REG_PWR_CTRL, &value, 1);
	if (rc != 0) {
		return rc;
	}
	if ((value & BMI270_PWR_CTRL_ACC_GYR_ENABLE) != BMI270_PWR_CTRL_ACC_GYR_ENABLE) {
		LOG_ERR("BMI270 sensors did not start, PWR_CTRL: 0x%02x", value);
		return -EIO;
	}

	return 0;
}

static int log_bmi270_sample(void)
{
	uint8_t data[12];
	int32_t acceleration_milli[3];
	int32_t angular_velocity_milli[3];
	int rc = bmi270_read(BMI270_REG_ACC_DATA, data, sizeof(data));
	if (rc != 0) {
		return rc;
	}

	for (size_t axis = 0; axis < ARRAY_SIZE(acceleration_milli); ++axis) {
		int16_t acceleration_raw = (int16_t)sys_get_le16(&data[axis * 2]);
		int16_t angular_velocity_raw = (int16_t)sys_get_le16(&data[6 + axis * 2]);

		acceleration_milli[axis] =
			(int32_t)(((int64_t)acceleration_raw * 2 * 9806650) / INT16_MAX / 1000);
		angular_velocity_milli[axis] =
			(int32_t)(((int64_t)angular_velocity_raw * 500 * 3141592) /
				  (180LL * INT16_MAX * 1000));
	}

	LOG_INF("BMI270 a_mm_s2=[%d,%d,%d] g_mrad_s=[%d,%d,%d]", acceleration_milli[0],
		acceleration_milli[1], acceleration_milli[2], angular_velocity_milli[0],
		angular_velocity_milli[1], angular_velocity_milli[2]);

	return 0;
}

static void button_callback(const struct device* dev, struct gpio_callback* cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	LOG_INF("Button pressed. Going to oops.");
	k_oops();
}

static int prepare_button(void)
{
	int rc;

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Button device %s is not ready", button.port->name);
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (rc != 0) {
		return rc;
	}

	rc = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc != 0) {
		return rc;
	}

	gpio_init_callback(&button_cb_data, button_callback, BIT(button.pin));
	return gpio_add_callback(button.port, &button_cb_data);
}

int main(void)
{
	int rc;

	LOG_INF("Starting Spotflow BLE BMI270 sample");

	rc = prepare_button();
	if (rc != 0) {
		LOG_ERR("Failed to prepare button: %d", rc);
		return rc;
	}

	rc = configure_bmi270();
	if (rc != 0) {
		LOG_ERR("Failed to configure BMI270: %d", rc);
		return rc;
	}

#if defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK) && \
	!defined(CONFIG_SPOTFLOW_METRICS_SYSTEM_STACK_ALL_THREADS)
	enable_thread_stack_metric();
#endif

	LOG_INF("BMI270 ready; reporting acceleration and angular velocity at 1 Hz");

	while (true) {
		k_sleep(K_SECONDS(1));

		rc = log_bmi270_sample();
		if (rc != 0) {
			LOG_WRN("Failed to read BMI270: %d", rc);
		}
	}

	return 0;
}
