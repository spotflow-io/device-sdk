#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ota/spotflow_ota.h"
#include "ota/spotflow_ota_persistence.h"
#include "ota/spotflow_ota_state.h"
#include "net/spotflow_mqtt.h"

#ifdef CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL
#define SPOTFLOW_OTA_LOG_LEVEL CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL
#else
#define SPOTFLOW_OTA_LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#endif

LOG_MODULE_REGISTER(spotflow_ota, SPOTFLOW_OTA_LOG_LEVEL);

static K_MUTEX_DEFINE(ota_mutex);
static bool ota_initialized;
static uint64_t last_received_attempt_id;

static void handle_ota_c2d_msg(uint8_t* payload, size_t len);

int spotflow_ota_init(void)
{
	k_mutex_lock(&ota_mutex, K_FOREVER);

	if (ota_initialized) {
		k_mutex_unlock(&ota_mutex);
		return 0;
	}

	int rc = spotflow_ota_persistence_init();
	if (rc < 0) {
		k_mutex_unlock(&ota_mutex);
		return rc;
	}

	struct spotflow_ota_persisted_attempt attempt;
	bool has_attempt;
	rc = spotflow_ota_persistence_load_attempt(&attempt, &has_attempt);
	if (rc < 0) {
		k_mutex_unlock(&ota_mutex);
		return rc;
	}

	struct spotflow_ota_probation probation;
	bool has_probation;
	rc = spotflow_ota_persistence_load_probation(&probation, &has_probation);
	if (rc < 0) {
		k_mutex_unlock(&ota_mutex);
		return rc;
	}

	/* TODO: Will be handled eventually */
	ARG_UNUSED(probation);
	ARG_UNUSED(has_probation);

	spotflow_ota_state_reset();
	last_received_attempt_id = has_attempt ? attempt.attempt_id : 0;
	ota_initialized = true;

	/* TODO: The attempt will be eventually propagated to the OTA state */

	k_mutex_unlock(&ota_mutex);
	return 0;
}

int spotflow_ota_init_session(void)
{
	int rc = spotflow_mqtt_request_ota_subscription(handle_ota_c2d_msg);
	if (rc < 0) {
		LOG_ERR("Failed to request subscription to OTA topic: %d", rc);
		return rc;
	}

	return 0;
}

uint64_t spotflow_ota_get_last_received_attempt_id(void)
{
	uint64_t attempt_id;

	k_mutex_lock(&ota_mutex, K_FOREVER);
	attempt_id = last_received_attempt_id;
	k_mutex_unlock(&ota_mutex);

	return attempt_id;
}

void spotflow_ota_reset(void)
{
	k_mutex_lock(&ota_mutex, K_FOREVER);
	ota_initialized = false;
	last_received_attempt_id = 0;
	k_mutex_unlock(&ota_mutex);

	spotflow_ota_state_reset();
}

static void handle_ota_c2d_msg(uint8_t* payload, size_t len)
{
	ARG_UNUSED(payload);
	ARG_UNUSED(len);
	LOG_DBG("Received OTA C2D payload before worker integration");
}
