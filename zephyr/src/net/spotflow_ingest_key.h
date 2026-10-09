#ifndef SPOTFLOW_INGEST_KEY_H
#define SPOTFLOW_INGEST_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resolve and cache the ingest key for the MQTT transport.
 *
 * @return The Kconfig key in static mode, or the cached application-provided key in
 *         dynamic mode. Returns NULL while the dynamic callback has not provided a key.
 */
const char* spotflow_get_ingest_key(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_INGEST_KEY_H */
