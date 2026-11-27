#ifndef SPOTFLOW_INGEST_KEY_PUBLIC_H
#define SPOTFLOW_INGEST_KEY_PUBLIC_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Provide the ingest key from application code.
 *
 * Enable CONFIG_SPOTFLOW_INGEST_KEY_DYNAMIC and implement this callback in your
 * application. CONFIG_SPOTFLOW_INGEST_KEY is unavailable in this mode.
 *
 * The SDK calls this callback from its processing thread before connecting over
 * MQTT or starting BLE advertising. If it returns NULL, the SDK defers the
 * connection or advertising and calls the callback again later.
 *
 * The SDK caches the first non-NULL pointer (including an empty string) until reboot
 * without copying the string, so do not change the string contents.
 * For BLE, return 1 to 512 bytes of printable ASCII (0x21 through 0x7E),
 * followed by the NUL terminator. An empty or oversized key prevents
 * Spotflow BLE advertising until reboot.
 *
 * @return A NUL-terminated ingest key, or NULL if the key is not available yet.
 */
const char* spotflow_on_ingest_key_requested(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_INGEST_KEY_PUBLIC_H */
