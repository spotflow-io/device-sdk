#include "ota/platform/spotflow_ota_platform.h"

#include "ota/platform/psa/spotflow_ota_psa_provider.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_ota, CONFIG_SPOTFLOW_OTA_LOG_LEVEL);

static K_MUTEX_DEFINE(psa_mutex);
static bool confirmation_pending_reboot;
static psa_fwu_component_t downloaded_component;

static int get_running_image_state(enum spotflow_ota_running_image_state* state);

int spotflow_ota_platform_init(void)
{
	k_mutex_lock(&psa_mutex, K_FOREVER);
	confirmation_pending_reboot = false;
	int rc = spotflow_ota_psa_init();

	k_mutex_unlock(&psa_mutex);
	return rc;
}

int spotflow_ota_platform_get_running_image_state(enum spotflow_ota_running_image_state* state)
{
	if (state == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&psa_mutex, K_FOREVER);
	int rc = get_running_image_state(state);

	k_mutex_unlock(&psa_mutex);
	return rc;
}

int spotflow_ota_platform_request_upgrade(void)
{
	k_mutex_lock(&psa_mutex, K_FOREVER);
	int rc = spotflow_ota_psa_check_install(downloaded_component);

	if (rc == 0) {
		psa_status_t status = psa_fwu_install();

		/* Installing the main executable must transfer execution by rebooting. */
		rc = status == PSA_SUCCESS_REBOOT
			? 0
			: (status < 0 ? spotflow_ota_psa_errno(status) : -ENOTSUP);
	}
	k_mutex_unlock(&psa_mutex);
	return rc;
}

int spotflow_ota_platform_confirm_image(bool* reboot_required)
{
	enum spotflow_ota_running_image_state state;
	int rc;

	if (reboot_required == NULL) {
		return -EINVAL;
	}
	*reboot_required = false;
	k_mutex_lock(&psa_mutex, K_FOREVER);
	rc = get_running_image_state(&state);
	if (rc < 0 || state == SPOTFLOW_OTA_RUNNING_IMAGE_CONFIRMED) {
		goto done;
	}
	if (state != SPOTFLOW_OTA_RUNNING_IMAGE_UNCONFIRMED) {
		rc = -EINVAL;
		goto done;
	}
	if (confirmation_pending_reboot) {
		*reboot_required = true;
		goto done;
	}
	rc = spotflow_ota_psa_check_accept();
	if (rc < 0) {
		goto done;
	}
	psa_status_t status = psa_fwu_accept();

	if (status == PSA_SUCCESS_REBOOT) {
		confirmation_pending_reboot = true;
		*reboot_required = true;
		LOG_INF("PSA confirmation requires an application-requested PSA reboot");
		goto done;
	}
	rc = spotflow_ota_psa_errno(status);
	if (rc == 0) {
		rc = get_running_image_state(&state);
		if (rc == 0 && state != SPOTFLOW_OTA_RUNNING_IMAGE_CONFIRMED) {
			rc = -EIO;
		}
	}
done:
	k_mutex_unlock(&psa_mutex);
	return rc;
}

int spotflow_ota_platform_reboot(void)
{
	k_mutex_lock(&psa_mutex, K_FOREVER);
	int rc = spotflow_ota_psa_errno(psa_fwu_request_reboot());

	k_mutex_unlock(&psa_mutex);
	return rc;
}

int spotflow_ota_platform_begin_image_write(void)
{
	k_mutex_lock(&psa_mutex, K_FOREVER);
	int rc = spotflow_ota_psa_begin();

	k_mutex_unlock(&psa_mutex);
	return rc;
}

int spotflow_ota_platform_write_image_block(const struct spotflow_artifact_block* block)
{
	k_mutex_lock(&psa_mutex, K_FOREVER);
	int rc = spotflow_ota_psa_write(block);

	k_mutex_unlock(&psa_mutex);
	return rc;
}

int spotflow_ota_platform_get_downloaded_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	if (build_id == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&psa_mutex, K_FOREVER);
	int rc = spotflow_ota_psa_end(&downloaded_component, build_id);

	k_mutex_unlock(&psa_mutex);
	return rc;
}

int spotflow_ota_platform_finish_image_write(void)
{
	k_mutex_lock(&psa_mutex, K_FOREVER);
	int rc = spotflow_ota_psa_errno(psa_fwu_finish(downloaded_component));

	k_mutex_unlock(&psa_mutex);
	return rc;
}

int spotflow_ota_platform_abort_image_write(void)
{
	k_mutex_lock(&psa_mutex, K_FOREVER);
	int rc = spotflow_ota_psa_abort();

	k_mutex_unlock(&psa_mutex);
	return rc;
}

static int get_running_image_state(enum spotflow_ota_running_image_state* state)
{
	psa_fwu_component_info_t info;
	int rc = spotflow_ota_psa_running_info(&info);

	if (rc < 0) {
		return rc;
	}
	switch (info.state) {
	case PSA_FWU_TRIAL:
		*state = SPOTFLOW_OTA_RUNNING_IMAGE_UNCONFIRMED;
		return 0;
	case PSA_FWU_UPDATED:
		*state = SPOTFLOW_OTA_RUNNING_IMAGE_CONFIRMED;
		return 0;
	case PSA_FWU_FAILED:
	case PSA_FWU_REJECTED:
		*state = SPOTFLOW_OTA_RUNNING_IMAGE_REJECTED;
		return 0;
	default:
		return -EIO;
	}
}
