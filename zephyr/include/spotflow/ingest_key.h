#ifndef SPOTFLOW_INGEST_KEY_PUBLIC_H
#define SPOTFLOW_INGEST_KEY_PUBLIC_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Provide the ingest key for the MQTT transport from application code.
 *
 * Override this weak callback to supply a provisioned ingest key at runtime.
 * The SDK calls it from its processing thread when preparing an MQTT connection.
 * If this function returns NULL, the SDK falls back to using CONFIG_SPOTFLOW_INGEST_KEY.
 *
 * When CONFIG_SPOTFLOW_INGEST_KEY is empty, the SDK will call this callback repeatedly
 * until it returns a non-NULL value. This ensures that a valid ingest key is eventually
 * obtained even in the case of transient issues during its reading.
 *
 * The SDK retains the returned pointer without copying the string, so keep it valid and
 * unchanged until reboot.
 *
 * @return A NUL-terminated ingest key, or NULL to use CONFIG_SPOTFLOW_INGEST_KEY.
 */
const char* spotflow_override_ingest_key(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_INGEST_KEY_PUBLIC_H */
