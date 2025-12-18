#include <stdio.h>
#include <inttypes.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"
#include "configs/spotflow_config_persistence.h"
#include "logging/spotflow_log_backend.h"
#include "configs/spotflow_config_cbor.h"

#define SPOTFLOW_SENT_LOG_LEVEL_KEY "Spotflow_S_L"
#define STORAGE_NAMESPACE "Spotflow"
/**
 * @brief
 *
 */
void spotflow_config_persistence_try_init(void)
{
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
#if CONFIG_SPOTFLOW_ERASE_NVS_FLASH
		SPOTFLOW_LOG("[CONFIG_PERSISTANCE] Initialize failed due to %s erasing nvs flash "
			     "and retrying \n",
			     esp_err_to_name(err));
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
#else
		SPOTFLOW_LOG("[CONFIG_PERSISTANCE] Initialize failed due to (%s). Please erase the "
			     "nvs flash and try again\n",
			     esp_err_to_name(err));
#endif
	}
	ESP_ERROR_CHECK(err);
}

/**
 * @brief
 *
 * @param settings
 */
void spotflow_config_persistence_try_load(struct spotflow_config_persisted_settings* settings)
{
	*settings = (struct spotflow_config_persisted_settings){ 0 };
	nvs_handle_t spotflow_handle;
	esp_err_t err;

	// Open
	err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &spotflow_handle);
	if (err != ESP_OK) {
		return;
	}

	err = nvs_get_u8(spotflow_handle, SPOTFLOW_SENT_LOG_LEVEL_KEY, &settings->sent_log_level);
	if (err != ESP_OK) {
		SPOTFLOW_LOG("Failed to read log_level! Error : (%s)\n", esp_err_to_name(err));
	} else {
		settings->flags |= SPOTFLOW_PERSISTED_SETTINGS_FLAG_SENT_LOG_LEVEL;
	}

	nvs_close(spotflow_handle);
}

/**
 * @brief
 *
 * @param settings
 */
void spotflow_config_persistence_try_save(struct spotflow_config_persisted_settings* settings)
{
	if (!settings->flags) {
		return;
	}

	nvs_handle_t spotflow_handle;
	esp_err_t err;

	// Open
	err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &spotflow_handle);
	if (err != ESP_OK) {
		SPOTFLOW_LOG("Failed to open NVS for read/write!\n");
		return;
	}

	err = nvs_set_u8(spotflow_handle, SPOTFLOW_SENT_LOG_LEVEL_KEY, settings->sent_log_level);
	if (err != ESP_OK) {
		nvs_close(spotflow_handle);
		SPOTFLOW_LOG("Failed to write log_level!. Error (%s)\n", esp_err_to_name(err));
		return;
	}

	err = nvs_commit(spotflow_handle);
	if (err != ESP_OK) {
		SPOTFLOW_LOG("Error (%s) committing data!\n", esp_err_to_name(err));
	}

	nvs_close(spotflow_handle);
}