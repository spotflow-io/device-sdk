#ifndef SPOTFLOW_OTA_PUBLIC_H
#define SPOTFLOW_OTA_PUBLIC_H

#include <stdbool.h>
#include <stdint.h>

#include "downloader.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Metadata for one firmware artifact in an OTA manifest.
 *
 * Passed to @ref spotflow_on_handle_firmware_update for artifacts handled by
 * application code. When CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE is
 * enabled, the main artifact is handled by the SDK instead; otherwise every
 * artifact, including main, is delivered through that callback.
 * String pointers and @ref spotflow_download_request fields refer to SDK-owned storage
 * that remains valid only for the duration of the callback unless noted otherwise.
 */
struct spotflow_firmware_info {
	uint64_t attempt_id;
	const char* slug;
	bool is_main;
	const struct spotflow_download_request* download_request;
	const char* version;
};

/** Terminal and in-progress results reported for an artifact update. */
enum spotflow_ota_result {
	/** Artifact or main update is still in progress. */
	SPOTFLOW_OTA_RESULT_PENDING,
	/** Artifact or main update finished successfully. */
	SPOTFLOW_OTA_RESULT_SUCCEEDED,
	/** Artifact or main update finished with an error. */
	SPOTFLOW_OTA_RESULT_FAILED,
	/** Artifact or main update was canceled. */
	SPOTFLOW_OTA_RESULT_CANCELED,
};

/** Lifecycle phase of an automatic main-firmware update. */
enum spotflow_ota_phase {
	/** No main-firmware update is active. */
	SPOTFLOW_OTA_PHASE_NOT_RUNNING,
	/** Main firmware is queued for download. */
	SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD,
	/** Main firmware image is downloading. */
	SPOTFLOW_OTA_PHASE_DOWNLOADING,
	/** Download finished; preparing MCUboot test upgrade. */
	SPOTFLOW_OTA_PHASE_PENDING_UPGRADE,
	/** Test upgrade requested; device is about to reboot. */
	SPOTFLOW_OTA_PHASE_PENDING_REBOOT,
	/** Device rebooted into the new image and awaits confirmation. */
	SPOTFLOW_OTA_PHASE_UNCONFIRMED,
};

/** Snapshot of automatic main-firmware update progress. */
struct spotflow_ota_main_firmware_state {
	enum spotflow_ota_phase phase;
	bool is_paused;
	enum spotflow_ota_result result;
};

/**
 * @brief Handle one firmware artifact delegated to application code.
 *
 * Invoked by the SDK for each manifest artifact that is not handled
 * automatically. When CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE is
 * enabled, the main artifact is handled internally and this callback receives
 * only the other artifacts; when that option is disabled, every artifact—
 * including main—is passed here. Use @p info->is_main to distinguish main
 * from non-main artifacts.
 *
 * The handler runs on the OTA worker thread and must return a terminal result:
 * @c SPOTFLOW_OTA_RESULT_SUCCEEDED, @c SPOTFLOW_OTA_RESULT_FAILED, or
 * @c SPOTFLOW_OTA_RESULT_CANCELED. Returning @c SPOTFLOW_OTA_RESULT_PENDING is
 * invalid and is treated as @c SPOTFLOW_OTA_RESULT_FAILED.
 *
 * Poll @ref spotflow_is_update_canceled() during long-running work. When a
 * download is active, call @ref spotflow_cancel_download() in response to
 * cancellation.
 *
 * The default (weak) implementation returns @c SPOTFLOW_OTA_RESULT_FAILED.
 *
 * @param info Artifact metadata and download credentials. Must not be NULL.
 *
 * @return Terminal result for this artifact.
 */
enum spotflow_ota_result
spotflow_on_handle_firmware_update(const struct spotflow_firmware_info* info);

/**
 * @brief Notify application code that the current update was canceled from the cloud.
 *
 * Called asynchronously on the system work queue after the SDK accepts a cloud
 * cancellation while no artifact in the attempt has yet succeeded.
 */
void spotflow_on_update_canceled(void);

/**
 * @brief Test whether cloud cancellation is actionable for the running artifact.
 *
 * Returns true after the SDK accepts a cancellation and until the currently
 * running artifact handler returns a terminal result. If an artifact later
 * reports @c SPOTFLOW_OTA_RESULT_SUCCEEDED, cancellation is no longer
 * actionable for the remaining artifacts unless the handler itself was canceled.
 *
 * Initializes OTA defensively before reading state.
 *
 * @return true when cancellation should be honored by the running handler,
 *         false otherwise
 .
 */
bool spotflow_is_update_canceled(void);

/**
 * @brief Report automatic main-firmware progress to application code.
 *
 * Available only when CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE
 * is enabled. Invoked from the OTA worker after SDK-driven state changes, such
 * as download completion or entering the unconfirmed phase. Not invoked for
 * changes caused directly by @ref spotflow_pause_main_firmware_update,
 * @ref spotflow_resume_main_firmware_update, or
 * @ref spotflow_fail_main_firmware_update.
 *
 * The default (weak) implementation is a no-op.
 *
 * @param state Current main-firmware state snapshot.
 */
void spotflow_on_main_firmware_update_progressed(
    const struct spotflow_ota_main_firmware_state* state);

/**
 * @brief Read the current automatic main-firmware update state.
 *
 * Initializes OTA defensively before reading state.
 *
 * @param state Out parameter for the current snapshot. Must not be NULL.
 *
 * @retval 0 State written successfully.
 * @retval -EINVAL @p state is NULL.
 * @retval <0 Negative errno when OTA initialization fails.
 */
int spotflow_get_main_firmware_update_state(struct spotflow_ota_main_firmware_state* state);

/**
 * @brief Read metadata for the main firmware artifact in the current attempt.
 *
 * Available only while the SDK holds the received manifest for the active
 * attempt. Manifest details are not persisted across reboot; after reboot,
 * query @ref spotflow_get_main_firmware_update_state instead.
 *
 * On success, @p info->download_request points to @p request. The @p request
 * struct is caller-owned and remains valid after the call returns. Its @c url
 * and @c secret fields point to SDK-owned strings that remain valid only until
 * the current attempt ends or its in-memory manifest is replaced.
 *
 * Initializes OTA defensively before reading state.
 *
 * @param info Out parameter for artifact metadata. Must not be NULL.
 * @param request Out parameter for download credentials. Must not be NULL.
 *
 * @retval 0 Metadata written successfully.
 * @retval -EINVAL @p info or @p request is NULL.
 * @retval -ENOENT No active attempt or no main-firmware artifact in the manifest.
 * @retval <0 Negative errno when OTA initialization fails.
 */
int spotflow_get_main_firmware_update_info(struct spotflow_firmware_info* info,
					   struct spotflow_download_request* request);

/**
 * @brief Pause the automatic main-firmware update.
 *
 * Requires CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE. Applies
 * only while the phase is @c SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD,
 * @c SPOTFLOW_OTA_PHASE_DOWNLOADING, @c SPOTFLOW_OTA_PHASE_PENDING_UPGRADE, or
 * @c SPOTFLOW_OTA_PHASE_PENDING_REBOOT. Pausing while already paused succeeds
 * and leaves the update paused.
 *
 * When @p state is not NULL, the current main-firmware state is written on
 * return, including after errors.
 *
 * Initializes OTA defensively before changing state.
 *
 * @param state Optional out parameter for the resulting snapshot.
 *
 * @retval 0 Update paused (or already paused).
 * @retval -EINVAL No active main-firmware update, or phase is
 *         @c SPOTFLOW_OTA_PHASE_NOT_RUNNING or @c SPOTFLOW_OTA_PHASE_UNCONFIRMED.
 * @retval -ENOTSUP Automatic main-firmware handling is disabled.
 * @retval <0 Negative errno when OTA initialization or the underlying download
 *         pause fails.
 */
int spotflow_pause_main_firmware_update(struct spotflow_ota_main_firmware_state* state);

/**
 * @brief Resume a paused automatic main-firmware update.
 *
 * Requires CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE. Succeeds
 * only while the update is currently paused.
 *
 * When @p state is not NULL, the current main-firmware state is written on
 * return, including after errors.
 *
 * Initializes OTA defensively before changing state.
 *
 * @param state Optional out parameter for the resulting snapshot.
 *
 * @retval 0 Update resumed.
 * @retval -EINVAL No active main-firmware update, or the update is not paused.
 * @retval -ENOTSUP Automatic main-firmware handling is disabled.
 * @retval <0 Negative errno when OTA initialization or the underlying download
 *         resume fails.
 */
int spotflow_resume_main_firmware_update(struct spotflow_ota_main_firmware_state* state);

/**
 * @brief Request failure of the automatic main-firmware update.
 *
 * Requires CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE. Applies
 * only while the phase is @c SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD,
 * @c SPOTFLOW_OTA_PHASE_DOWNLOADING, or @c SPOTFLOW_OTA_PHASE_PENDING_UPGRADE.
 * Cannot fail an update in @c SPOTFLOW_OTA_PHASE_PENDING_REBOOT because the
 * MCUboot test upgrade has already been requested, or in
 * @c SPOTFLOW_OTA_PHASE_UNCONFIRMED; use @ref spotflow_confirm_main_firmware_image
 * after reboot or allow rollback handling on the next boot.
 *
 * When a download is active or the update is paused between phases, the call
 * returns @c 0 immediately and the failure completes asynchronously.
 *
 * When @p state is not NULL, the current main-firmware state is written on
 * return, including after errors.
 *
 * Initializes OTA defensively before changing state.
 *
 * @param state Optional out parameter for the resulting snapshot.
 *
 * @retval 0 Failure accepted (completed now or asynchronously).
 * @retval -EINVAL No active main-firmware update, or phase is
 *         @c SPOTFLOW_OTA_PHASE_NOT_RUNNING, @c SPOTFLOW_OTA_PHASE_PENDING_REBOOT, or
 *         @c SPOTFLOW_OTA_PHASE_UNCONFIRMED.
 * @retval -ENOTSUP Automatic main-firmware handling is disabled.
 * @retval <0 Negative errno when OTA initialization or persisting the failure
 *         result fails.
 */
int spotflow_fail_main_firmware_update(struct spotflow_ota_main_firmware_state* state);

/**
 * @brief Confirm the running main-firmware image after a test upgrade.
 *
 * Requires CONFIG_SPOTFLOW_OTA_AUTO_HANDLE_MAIN_FIRMWARE. After a
 * successful reboot into an unconfirmed image, confirms the image with MCUboot,
 * reports success to the cloud, and clears probation metadata.
 *
 * If the image is already confirmed and the stored result is
 * @c SPOTFLOW_OTA_RESULT_SUCCEEDED, the call succeeds without doing further
 * work.
 *
 * When @p state is not NULL, the current main-firmware state is written on
 * return, including after errors.
 *
 * Initializes OTA defensively before changing state.
 *
 * @param state Optional out parameter for the resulting snapshot.
 *
 * @retval 0 Image confirmed, or already confirmed.
 * @retval -EINVAL No active attempt, wrong phase, or probation metadata does not
 *         match the current attempt.
 * @retval -ENOTSUP Automatic main-firmware handling is disabled.
 * @retval <0 Negative errno when OTA initialization, platform confirmation, or
 *         persistence fails.
 */
int spotflow_confirm_main_firmware_image(struct spotflow_ota_main_firmware_state* state);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_PUBLIC_H */
