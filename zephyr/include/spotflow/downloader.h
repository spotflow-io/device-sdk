#ifndef SPOTFLOW_DOWNLOADER_H
#define SPOTFLOW_DOWNLOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** State of a @ref spotflow_downloader instance. */
enum spotflow_downloader_state {
	/** No download is active. */
	SPOTFLOW_DOWNLOADER_STATE_INACTIVE,
	/** A download is in progress. */
	SPOTFLOW_DOWNLOADER_STATE_DOWNLOADING,
	/** A download is paused. */
	SPOTFLOW_DOWNLOADER_STATE_PAUSED,
	/** A download is being canceled. */
	SPOTFLOW_DOWNLOADER_STATE_CANCELING,
};

/**
 * @brief Downloader instance for streaming one OTA artifact over HTTP(S).
 *
 * Allocate statically with @ref SPOTFLOW_DEFINE_DOWNLOADER. Fields are visible
 * so applications can embed the downloader, but they are owned by the SDK and
 * must not be modified by application code.
 */
struct spotflow_downloader {
	enum spotflow_downloader_state state;
	struct k_mutex mutex;
	bool cancel_requested;
	struct k_sem resume_sem;
};

/** Define and statically initialize a @ref spotflow_downloader. */
#define SPOTFLOW_DEFINE_DOWNLOADER(name)                                \
	struct spotflow_downloader name = {                             \
		.state = SPOTFLOW_DOWNLOADER_STATE_INACTIVE,            \
		.mutex = Z_MUTEX_INITIALIZER(name.mutex),               \
		.resume_sem = Z_SEM_INITIALIZER(name.resume_sem, 0, 1), \
	}

/** One block of artifact bytes delivered during a download. */
struct spotflow_artifact_block {
	/** Byte offset of @p data within the artifact. */
	size_t offset;
	const uint8_t* data;
	size_t data_len;
	/** True when this is the final block ( @p data_len may be zero ). */
	bool is_last;
};

/** Credentials and location for downloading one artifact. */
struct spotflow_download_request {
	const char* url;
	/** OTA secret used for the HTTP Authorization header. Must not be logged. */
	const char* secret;
};

/**
 * @brief Receive one downloaded artifact block.
 *
 * Invoked synchronously from the thread that called
 * @ref spotflow_download_artifact. The callback runs until the download
 * finishes, is canceled, or fails.
 *
 * @param block Current block. Valid only for the duration of the call.
 * @param downloader Downloader instance performing the transfer.
 * @param callback_ctx User context passed to @ref spotflow_download_artifact.
 */
typedef void (*spotflow_download_block_callback)(const struct spotflow_artifact_block* block,
						 struct spotflow_downloader* downloader,
						 void* callback_ctx);

/**
 * @brief Download one artifact sequentially over HTTP(S).
 *
 * Blocks the calling thread until the download completes, is canceled, or
 * fails. Transient transport errors are retried automatically using HTTP
 * Range requests to resume from the last received byte.
 *
 * Only one download may run on a given downloader at a time.
 *
 * @param downloader Downloader instance. Must not be NULL.
 * @param request Download URL and secret. Both fields must not be NULL.
 * @param callback Block handler invoked for each received chunk.
 * @param callback_ctx Opaque context passed to @p callback.
 *
 * @retval 0 Download completed and the final block was delivered.
 * @retval -ECANCELED Download was canceled via @ref spotflow_cancel_download.
 * @retval -EBUSY A download is already active on @p downloader.
 * @retval -EINVAL Invalid arguments or an unsupported or malformed URL.
 * @retval <0 Other negative errno on non-retryable transport or protocol failure.
 */
int spotflow_download_artifact(struct spotflow_downloader* downloader,
			       const struct spotflow_download_request* request,
			       spotflow_download_block_callback callback, void* callback_ctx);

/**
 * @brief Read the current downloader state.
 *
 * @param downloader Downloader instance. When NULL, returns
 *         @c SPOTFLOW_DOWNLOADER_STATE_INACTIVE.
 */
enum spotflow_downloader_state
spotflow_get_downloader_state(const struct spotflow_downloader* downloader);

/**
 * @brief Pause an active download.
 *
 * @param downloader Downloader instance. Must not be NULL.
 *
 * @retval 0 Download paused.
 * @retval -EINVAL @p downloader is NULL or not in
 *         @c SPOTFLOW_DOWNLOADER_STATE_DOWNLOADING.
 */
int spotflow_pause_download(struct spotflow_downloader* downloader);

/**
 * @brief Resume a paused download.
 *
 * @param downloader Downloader instance. Must not be NULL.
 *
 * @retval 0 Download resumed.
 * @retval -EINVAL @p downloader is NULL or not in
 *         @c SPOTFLOW_DOWNLOADER_STATE_PAUSED.
 */
int spotflow_resume_download(struct spotflow_downloader* downloader);

/**
 * @brief Cancel an active or paused download.
 *
 * @param downloader Downloader instance. Must not be NULL.
 *
 * @retval 0 Cancel requested; @ref spotflow_download_artifact returns
 *         @c -ECANCELED.
 * @retval -EINVAL @p downloader is NULL or in
 *         @c SPOTFLOW_DOWNLOADER_STATE_INACTIVE.
 */
int spotflow_cancel_download(struct spotflow_downloader* downloader);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_DOWNLOADER_H */
