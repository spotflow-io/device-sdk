#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "ota/spotflow_ota_persistence.h"

LOG_MODULE_DECLARE(spotflow_ota);

#define SPOTFLOW_OTA_SETTINGS_ROOT "spotflow/ota"
#define SPOTFLOW_OTA_SETTINGS_KEY_ATTEMPT "attempt"
#define SPOTFLOW_OTA_SETTINGS_KEY_PROBATION "probation"
#define SPOTFLOW_OTA_SETTINGS_KEY_VERSION "version"
#define SPOTFLOW_OTA_SETTINGS_PATH_ATTEMPT \
	SPOTFLOW_OTA_SETTINGS_ROOT "/" SPOTFLOW_OTA_SETTINGS_KEY_ATTEMPT
#define SPOTFLOW_OTA_SETTINGS_PATH_PROBATION \
	SPOTFLOW_OTA_SETTINGS_ROOT "/" SPOTFLOW_OTA_SETTINGS_KEY_PROBATION
#define SPOTFLOW_OTA_SETTINGS_PATH_VERSION_ROOT \
	SPOTFLOW_OTA_SETTINGS_ROOT "/" SPOTFLOW_OTA_SETTINGS_KEY_VERSION

#define SPOTFLOW_OTA_MAX_RECORD_SIZE 192

struct attempt_load_context {
	struct spotflow_ota_persisted_attempt* attempt;
	bool* has_attempt;
};

struct probation_load_context {
	struct spotflow_ota_probation* probation;
	bool* has_probation;
};

struct version_load_context {
	const char* slug;
	char* version;
	size_t version_len;
	bool* has_version;
};

static K_MUTEX_DEFINE(persistence_mutex);

static int load_attempt_callback(const char* key, size_t len, settings_read_cb read_cb,
				 void* cb_arg, void* param);
static int load_probation_callback(const char* key, size_t len, settings_read_cb read_cb,
				   void* cb_arg, void* param);
static int load_version_callback(const char* key, size_t len, settings_read_cb read_cb,
				 void* cb_arg, void* param);
static bool validate_settings_slug(const char* slug);

int spotflow_ota_persistence_init(void)
{
	k_mutex_lock(&persistence_mutex, K_FOREVER);
	int rc = settings_subsys_init();
	k_mutex_unlock(&persistence_mutex);

	if (rc < 0) {
		LOG_ERR("Failed to initialize OTA settings: %d", rc);
	}

	return rc;
}

int spotflow_ota_persistence_load_attempt(struct spotflow_ota_persisted_attempt* attempt,
					  bool* has_attempt)
{
	if (attempt == NULL || has_attempt == NULL) {
		return -EINVAL;
	}

	*attempt = (struct spotflow_ota_persisted_attempt){ 0 };
	*has_attempt = false;
	for (size_t i = 0; i < ARRAY_SIZE(attempt->artifact_results); i++) {
		attempt->artifact_results[i] = SPOTFLOW_OTA_RESULT_PENDING;
	}

	struct attempt_load_context context = {
		.attempt = attempt,
		.has_attempt = has_attempt,
	};

	k_mutex_lock(&persistence_mutex, K_FOREVER);
	int rc = settings_load_subtree_direct(SPOTFLOW_OTA_SETTINGS_ROOT, load_attempt_callback,
					      &context);
	k_mutex_unlock(&persistence_mutex);

	return rc;
}

int spotflow_ota_persistence_save_attempt(const struct spotflow_ota_persisted_attempt* attempt)
{
	if (attempt == NULL) {
		return -EINVAL;
	}

	uint8_t buffer[SPOTFLOW_OTA_MAX_RECORD_SIZE];
	size_t encoded_len;
	int rc =
	    spotflow_ota_records_cbor_encode_attempt(attempt, buffer, sizeof(buffer), &encoded_len);

	if (rc < 0) {
		return rc;
	}

	k_mutex_lock(&persistence_mutex, K_FOREVER);
	rc = settings_save_one(SPOTFLOW_OTA_SETTINGS_PATH_ATTEMPT, buffer, encoded_len);
	k_mutex_unlock(&persistence_mutex);
	return rc;
}

int spotflow_ota_persistence_load_probation(struct spotflow_ota_probation* probation,
					    bool* has_probation)
{
	if (probation == NULL || has_probation == NULL) {
		return -EINVAL;
	}

	*probation = (struct spotflow_ota_probation){ 0 };
	*has_probation = false;

	struct probation_load_context context = {
		.probation = probation,
		.has_probation = has_probation,
	};

	k_mutex_lock(&persistence_mutex, K_FOREVER);
	int rc = settings_load_subtree_direct(SPOTFLOW_OTA_SETTINGS_ROOT, load_probation_callback,
					      &context);
	k_mutex_unlock(&persistence_mutex);

	return rc;
}

int spotflow_ota_persistence_save_probation(const struct spotflow_ota_probation* probation)
{
	if (probation == NULL) {
		return -EINVAL;
	}

	uint8_t buffer[SPOTFLOW_OTA_MAX_RECORD_SIZE];
	size_t encoded_len;
	int rc = spotflow_ota_records_cbor_encode_probation(probation, buffer, sizeof(buffer),
							    &encoded_len);

	if (rc < 0) {
		return rc;
	}

	k_mutex_lock(&persistence_mutex, K_FOREVER);
	rc = settings_save_one(SPOTFLOW_OTA_SETTINGS_PATH_PROBATION, buffer, encoded_len);
	k_mutex_unlock(&persistence_mutex);
	return rc;
}

int spotflow_ota_persistence_clear_probation(void)
{
	k_mutex_lock(&persistence_mutex, K_FOREVER);
	int rc = settings_delete(SPOTFLOW_OTA_SETTINGS_PATH_PROBATION);
	k_mutex_unlock(&persistence_mutex);
	return rc;
}

int spotflow_ota_persistence_load_installed_version(const char* slug, char* version,
						    size_t version_len, bool* has_version)
{
	if (!validate_settings_slug(slug) || version == NULL || version_len == 0 ||
	    has_version == NULL) {
		return -EINVAL;
	}

	version[0] = '\0';
	*has_version = false;

	struct version_load_context context = {
		.slug = slug,
		.version = version,
		.version_len = version_len,
		.has_version = has_version,
	};

	k_mutex_lock(&persistence_mutex, K_FOREVER);
	int rc = settings_load_subtree_direct(SPOTFLOW_OTA_SETTINGS_ROOT, load_version_callback,
					      &context);
	k_mutex_unlock(&persistence_mutex);

	return rc;
}

int spotflow_ota_persistence_save_installed_version(const char* slug, const char* version)
{
	if (!validate_settings_slug(slug) || version == NULL || version[0] == '\0' ||
	    strlen(version) > SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH) {
		return -EINVAL;
	}

	char path[SETTINGS_FULL_NAME_LEN];
	int path_len =
	    snprintk(path, sizeof(path), "%s/%s", SPOTFLOW_OTA_SETTINGS_PATH_VERSION_ROOT, slug);

	if (path_len <= 0 || path_len >= sizeof(path)) {
		return -EINVAL;
	}

	k_mutex_lock(&persistence_mutex, K_FOREVER);
	int rc = settings_save_one(path, version, strlen(version) + 1);
	k_mutex_unlock(&persistence_mutex);
	return rc;
}

static int load_attempt_callback(const char* key, size_t len, settings_read_cb read_cb,
				 void* cb_arg, void* param)
{
	if (strcmp(key, SPOTFLOW_OTA_SETTINGS_KEY_ATTEMPT) != 0) {
		return 0;
	}

	struct attempt_load_context* context = param;
	uint8_t buffer[SPOTFLOW_OTA_MAX_RECORD_SIZE];

	if (len > sizeof(buffer)) {
		LOG_ERR("Ignoring oversized OTA attempt record");
		return 0;
	}

	int rc = read_cb(cb_arg, buffer, len);

	if (rc < 0 || (size_t)rc != len) {
		LOG_ERR("Failed to read OTA attempt record: %d", rc);
		return 0;
	}

	rc = spotflow_ota_records_cbor_decode_attempt(buffer, len, context->attempt);
	if (rc < 0) {
		LOG_ERR("Ignoring corrupt OTA attempt record: %d", rc);
		*context->has_attempt = false;
		*context->attempt = (struct spotflow_ota_persisted_attempt){ 0 };
		for (size_t i = 0; i < ARRAY_SIZE(context->attempt->artifact_results); i++) {
			context->attempt->artifact_results[i] = SPOTFLOW_OTA_RESULT_PENDING;
		}
		return 0;
	}

	*context->has_attempt = true;
	return 0;
}

static int load_probation_callback(const char* key, size_t len, settings_read_cb read_cb,
				   void* cb_arg, void* param)
{
	if (strcmp(key, SPOTFLOW_OTA_SETTINGS_KEY_PROBATION) != 0) {
		return 0;
	}

	struct probation_load_context* context = param;
	uint8_t buffer[SPOTFLOW_OTA_MAX_RECORD_SIZE];

	if (len > sizeof(buffer)) {
		LOG_ERR("Ignoring oversized OTA probation record");
		return 0;
	}

	int rc = read_cb(cb_arg, buffer, len);

	if (rc < 0 || (size_t)rc != len) {
		LOG_ERR("Failed to read OTA probation record: %d", rc);
		return 0;
	}

	rc = spotflow_ota_records_cbor_decode_probation(buffer, len, context->probation);
	if (rc < 0) {
		LOG_ERR("Ignoring corrupt OTA probation record: %d", rc);
		*context->has_probation = false;
		*context->probation = (struct spotflow_ota_probation){ 0 };
		return 0;
	}

	*context->has_probation = true;
	return 0;
}

static int load_version_callback(const char* key, size_t len, settings_read_cb read_cb,
				 void* cb_arg, void* param)
{
	struct version_load_context* context = param;
	char expected_key[SETTINGS_MAX_NAME_LEN + 1];
	int expected_key_len = snprintk(expected_key, sizeof(expected_key), "%s/%s",
					SPOTFLOW_OTA_SETTINGS_KEY_VERSION, context->slug);

	if (expected_key_len <= 0 || expected_key_len >= sizeof(expected_key) ||
	    strcmp(key, expected_key) != 0) {
		return 0;
	}

	if (len == 0 || len > context->version_len ||
	    len > SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1) {
		LOG_ERR("Ignoring invalid OTA installed version length");
		return 0;
	}

	int rc = read_cb(cb_arg, context->version, len);

	if (rc < 0 || (size_t)rc != len || context->version[len - 1] != '\0') {
		LOG_ERR("Failed to read OTA installed version");
		context->version[0] = '\0';
		return 0;
	}

	*context->has_version = true;
	return 0;
}

static bool validate_settings_slug(const char* slug)
{
	if (slug == NULL || slug[0] == '\0' ||
	    strlen(slug) > SPOTFLOW_OTA_ARTIFACT_SLUG_MAX_LENGTH) {
		return false;
	}

	return strchr(slug, '/') == NULL;
}
