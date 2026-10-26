#ifndef SPOTFLOW_OTA_PSA_PROVIDER_H
#define SPOTFLOW_OTA_PSA_PROVIDER_H

#include "spotflow_build_id.h"

#include <errno.h>
#include <spotflow/downloader.h>

#if defined(CONFIG_SPOTFLOW_OTA_PSA_FWU_TI_CC35XX)
#include <fwu/1.0/psa/update.h>
#else
#include <psa/update.h>
#endif

#if PSA_FWU_API_VERSION_MAJOR != 1
#error "Spotflow requires the PSA Firmware Update 1.x lifecycle API"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Internal PSA implementation boundary. The PSA platform serializes these calls.
 * The application must give Spotflow exclusive ownership of the FWU service.
 * A provider selects the main component and prepares its native image stream.
 * The PSA platform owns finish/install/accept; the coordinator owns probation.
 */
/**
 * @brief Initialize the provider.
 * @return Zero on success, negative errno on failure.
 */
int spotflow_ota_psa_init(void);
/**
 * @brief Query the component containing the running executable.
 * @param info Receives the native component information.
 * @return Zero on success, negative errno on invalid arguments or query failure.
 */
int spotflow_ota_psa_running_info(psa_fwu_component_info_t* info);
/**
 * @brief Select and prepare inactive storage for the main executable.
 * @return Zero on success, negative errno if no safe target is available.
 */
int spotflow_ota_psa_begin(void);
/**
 * @brief Consume the next sequential native artifact block.
 * @param block Downloaded block, including container bytes.
 * @return Zero on success, negative errno on invalid input or write failure.
 */
int spotflow_ota_psa_write(const struct spotflow_artifact_block* block);
/**
 * @brief Read a completed candidate before PSA finish.
 * @param component Receives the provider-selected component ID.
 * @param build_id Receives the executable identity extracted from the stream.
 * @return Zero on success, negative errno for an incomplete or invalid artifact.
 */
int spotflow_ota_psa_end(psa_fwu_component_t* component,
			 uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);
/**
 * @brief Discard the owned inactive candidate.
 * @return Zero on success, negative errno on cleanup failure.
 */
int spotflow_ota_psa_abort(void);
/**
 * @brief Ensure that global installation affects only the owned candidate.
 * @param component Candidate selected by this provider.
 * @return Zero on success, negative errno if another update blocks installation.
 */
int spotflow_ota_psa_check_install(psa_fwu_component_t component);
/**
 * @brief Ensure that global acceptance affects only the running main executable.
 * @return Zero on success, negative errno if another update blocks acceptance.
 */
int spotflow_ota_psa_check_accept(void);

/* Use only for operations requiring PSA_SUCCESS; reboot statuses are handled
 * explicitly by the lifecycle, rather than mapped to immediate completion.
 */
/**
 * @brief Translate an ordinary PSA operation result to errno.
 * @param status Native operation result; reboot statuses require separate handling.
 * @return Zero for PSA_SUCCESS, negative errno otherwise.
 */
static inline int spotflow_ota_psa_errno(psa_status_t status)
{
	switch (status) {
	case PSA_SUCCESS:
		return 0;
	case PSA_ERROR_NOT_PERMITTED:
		return -EPERM;
	case PSA_ERROR_NOT_SUPPORTED:
		return -ENOTSUP;
	case PSA_ERROR_INVALID_ARGUMENT:
	case PSA_ERROR_BAD_STATE:
		return -EINVAL;
	case PSA_ERROR_DOES_NOT_EXIST:
		return -ENOENT;
	case PSA_ERROR_INSUFFICIENT_MEMORY:
		return -ENOMEM;
	case PSA_ERROR_INSUFFICIENT_STORAGE:
		return -ENOSPC;
	case PSA_ERROR_DATA_CORRUPT:
	case PSA_ERROR_DATA_INVALID:
		return -EBADMSG;
	default:
		return -EIO;
	}
}

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_PSA_PROVIDER_H */
