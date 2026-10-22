#ifndef SPOTFLOW_OTA_PSA_H
#define SPOTFLOW_OTA_PSA_H

#include <errno.h>
#include <spotflow/downloader.h>
#include "spotflow_build_id.h"

#if defined(CONFIG_SPOTFLOW_OTA_PSA_FWU_TI_CC35XX)
#include <fwu/1.0/psa/update.h>
#else
#include <psa/update.h>
#endif

#if PSA_FWU_API_VERSION_MAJOR != 1
#error "Spotflow requires the PSA Firmware Update 1.x lifecycle API"
#endif

/* PSA-only provider boundary. Calls are serialized by the firmware handler.
 * The application must give Spotflow exclusive ownership of the FWU service.
 * A provider selects the main component and prepares its native image stream.
 * The handler owns finish/install/accept and the existing probation lifecycle.
 */
int spotflow_ota_psa_init(void);
int spotflow_ota_psa_running_info(psa_fwu_component_info_t* info);
int spotflow_ota_psa_begin(void);
int spotflow_ota_psa_write(const struct spotflow_artifact_block* block);
int spotflow_ota_psa_end(psa_fwu_component_t* component,
			 uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH]);
int spotflow_ota_psa_abort(void);
int spotflow_ota_psa_check_install(psa_fwu_component_t component);
int spotflow_ota_psa_check_accept(void);

/* Use only for operations requiring PSA_SUCCESS; reboot statuses are handled
 * explicitly by the lifecycle, rather than mapped to immediate completion.
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

#endif /* SPOTFLOW_OTA_PSA_H */
