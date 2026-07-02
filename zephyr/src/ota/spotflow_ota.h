#ifndef SPOTFLOW_OTA_H
#define SPOTFLOW_OTA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Perform one-time OTA initialization.
 *
 * Loads persisted OTA records, restores in-memory state, starts the OTA worker, and runs
 * main-firmware startup reconciliation when enabled. Idempotent and mutex-protected.
 *
 * Called by the Spotflow processor before session metadata and defensively from public
 * facade APIs in `spotflow/ota.h` that read or change OTA state.
 *
 * @return 0 on success, negative errno on failure.
 */
int spotflow_ota_init(void);

/**
 * @brief Initialize an OTA session by subscribing to the OTA MQTT topic.
 *
 * Calls @ref spotflow_ota_init() and registers the inbound OTA C2D handler. Safe to call
 * on every MQTT connect; init itself runs at most once per boot unless reset in tests.
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: MQTT subscription request failed
 */
int spotflow_ota_init_session();

/**
 * @brief Send one pending OTA D2C message, if any.
 *
 * Called from the MQTT processing loop.
 */
int spotflow_ota_send_pending_message(void);

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
