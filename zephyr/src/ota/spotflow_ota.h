#ifndef SPOTFLOW_OTA_H
#define SPOTFLOW_OTA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Perform one-time OTA initialization.
 *
 * Loads persisted OTA records needed before the first session metadata publish.
 *
 * @return 0 on success, negative errno on failure.
 */
int spotflow_ota_init(void);

/**
 * @brief Initialize an OTA session by subscribing to the OTA MQTT topic
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: MQTT subscription request failed
 */
int spotflow_ota_init_session();

/**
 * @brief Get the latest OTA attempt ID known to the SDK.
 *
 * Returns 0 when no OTA attempt has been recorded.
 */
uint64_t spotflow_ota_get_last_received_attempt_id(void);

/**
 * @brief Reset OTA facade state.
 *
 * Intended for unit tests.
 */
void spotflow_ota_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_H */
