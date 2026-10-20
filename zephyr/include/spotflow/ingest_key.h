#ifndef SPOTFLOW_INGEST_KEY_PUBLIC_H
#define SPOTFLOW_INGEST_KEY_PUBLIC_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Provide the ingest key for the MQTT transport from application code.
 *
 * Enable CONFIG_SPOTFLOW_INGEST_KEY_DYNAMIC and implement this callback in your
 * application. CONFIG_SPOTFLOW_INGEST_KEY is unavailable in this mode.
 *
 * The SDK calls this callback from its processing thread when preparing an MQTT
 * connection. If it returns NULL, the SDK skips the connection attempt and retries
 * later, calling the callback again until it returns a non-NULL value.
 *
 * The SDK caches the first non-NULL pointer (including an empty string) until reboot
 * without copying the string, so do not change the string contents.
 *
 * @return A NUL-terminated ingest key, or NULL if the key is not available yet.
 */
const char* spotflow_on_ingest_key_requested(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_INGEST_KEY_PUBLIC_H */
