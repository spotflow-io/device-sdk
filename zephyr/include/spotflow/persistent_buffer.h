#ifndef SPOTFLOW_PERSISTENT_BUFFER_PUBLIC_H
#define SPOTFLOW_PERSISTENT_BUFFER_PUBLIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Usage statistics of the persistent buffer. */
struct spotflow_persistent_buffer_stats {
	/** Bytes of the flash partition occupied by the ring, including sent data in sectors
	 *  that are not erased yet. */
	size_t bytes_used;
	/** Bytes of the flash partition available before the oldest data is dropped. */
	size_t bytes_free;
	/** Logs and metrics stored in flash and not sent yet. */
	uint32_t messages_stored;
	/** Logs and metrics dropped by the persistent buffer since boot. */
	uint32_t messages_dropped;
};

/**
 * @brief Write all queued RAM messages to flash.
 *
 * Returns after the messages are written. Call it from low-battery or shutdown hooks so that
 * the last messages survive a power loss. Requires CONFIG_SPOTFLOW_PERSISTENT_BUFFER.
 *
 * @retval 0 Success.
 * @retval -EAGAIN The buffer is not initialized yet.
 * @retval -ENODEV The buffer failed to initialize, for example due to a missing partition.
 * @retval <0 Other negative errno on a flash error.
 */
int spotflow_persistent_buffer_flush(void);

/**
 * @brief Get usage statistics of the persistent buffer.
 *
 * Reads the flash to count stored messages, so avoid calling it frequently.
 *
 * @param stats Output statistics.
 *
 * @retval 0 Success.
 * @retval -EINVAL @p stats is NULL.
 * @retval -EAGAIN The buffer is not initialized yet.
 * @retval -ENODEV The buffer failed to initialize.
 */
int spotflow_persistent_buffer_get_stats(struct spotflow_persistent_buffer_stats* stats);

/**
 * @brief Erase all data stored in the persistent buffer.
 *
 * Messages that are still queued in RAM are kept.
 *
 * @retval 0 Success.
 * @retval -EAGAIN The buffer is not initialized yet.
 * @retval -ENODEV The buffer failed to initialize.
 * @retval <0 Other negative errno on a flash error.
 */
int spotflow_persistent_buffer_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_PERSISTENT_BUFFER_PUBLIC_H */
