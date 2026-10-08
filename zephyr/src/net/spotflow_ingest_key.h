#ifndef SPOTFLOW_INGEST_KEY_H
#define SPOTFLOW_INGEST_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resolve and cache the ingest key for the MQTT transport.
 *
 * @return The non-NULL application-provided key (including an empty string),
 *         or a nonempty Kconfig key. Returns NULL if neither is available.
 */
const char* spotflow_get_ingest_key(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_INGEST_KEY_H */
