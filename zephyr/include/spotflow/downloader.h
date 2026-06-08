#ifndef SPOTFLOW_DOWNLOADER_H
#define SPOTFLOW_DOWNLOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

enum spotflow_downloader_state {
	SPOTFLOW_DOWNLOADER_STATE_INACTIVE,
	SPOTFLOW_DOWNLOADER_STATE_DOWNLOADING,
	SPOTFLOW_DOWNLOADER_STATE_PAUSED,
	SPOTFLOW_DOWNLOADER_STATE_CANCELING,
};

struct spotflow_downloader {
	/*
	 * Publicly visible so applications can allocate the downloader statically.
	 * The fields are owned by the SDK and should not be modified by user code.
	 */
	enum spotflow_downloader_state state;
	struct k_mutex mutex;
	bool cancel_requested;
};

#define SPOTFLOW_DEFINE_DOWNLOADER(name)                     \
	struct spotflow_downloader name = {                  \
		.state = SPOTFLOW_DOWNLOADER_STATE_INACTIVE, \
		.mutex = Z_MUTEX_INITIALIZER(name.mutex),    \
	}

struct spotflow_artifact_block {
	size_t offset;
	const uint8_t* data;
	size_t data_len;
	bool is_last;
};

struct spotflow_download_request {
	const char* url;
	/* Must not be logged by application or SDK code. */
	const char* secret;
};

typedef void (*spotflow_download_block_callback)(const struct spotflow_artifact_block* block,
						 struct spotflow_downloader* downloader,
						 void* callback_ctx);

int spotflow_download_artifact(struct spotflow_downloader* downloader,
			       const struct spotflow_download_request* request,
			       spotflow_download_block_callback callback, void* callback_ctx);

enum spotflow_downloader_state
spotflow_get_downloader_state(const struct spotflow_downloader* downloader);

int spotflow_pause_download(struct spotflow_downloader* downloader);

int spotflow_resume_download(struct spotflow_downloader* downloader);

int spotflow_cancel_download(struct spotflow_downloader* downloader);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_DOWNLOADER_H */
