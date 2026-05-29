#ifndef SPOTFLOW_OTA_H
#define SPOTFLOW_OTA_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize an OTA session by subscribing to the OTA MQTT topic
 *
 * @return 0 on success, negative errno on failure
 *         -EINVAL: MQTT subscription request failed
 */
int spotflow_ota_init_session();

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_H */
