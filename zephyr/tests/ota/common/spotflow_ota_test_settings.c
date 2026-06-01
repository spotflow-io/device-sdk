#include <string.h>

#include <zephyr/ztest.h>

#include "spotflow_ota_test_settings.h"

struct fake_setting_entry {
	bool in_use;
	char name[SETTINGS_FULL_NAME_LEN];
	uint8_t value[192];
	size_t value_len;
};

static struct fake_setting_entry fake_entries[8];
static char last_saved_name[SETTINGS_FULL_NAME_LEN];
static char last_deleted_name[SETTINGS_FULL_NAME_LEN];

static ssize_t fake_settings_read(void* cb_arg, void* data, size_t len)
{
	struct fake_setting_entry* entry = cb_arg;

	if (len > entry->value_len) {
		len = entry->value_len;
	}

	memcpy(data, entry->value, len);
	return len;
}

void spotflow_ota_test_settings_reset(void)
{
	memset(fake_entries, 0, sizeof(fake_entries));
	memset(last_saved_name, 0, sizeof(last_saved_name));
	memset(last_deleted_name, 0, sizeof(last_deleted_name));
}

const char* spotflow_ota_test_settings_get_last_saved_name(void)
{
	return last_saved_name;
}

const char* spotflow_ota_test_settings_get_last_deleted_name(void)
{
	return last_deleted_name;
}

int settings_subsys_init(void)
{
	return 0;
}

int settings_save_one(const char* name, const void* value, size_t val_len)
{
	for (size_t i = 0; i < ARRAY_SIZE(fake_entries); i++) {
		if (!fake_entries[i].in_use || strcmp(fake_entries[i].name, name) == 0) {
			fake_entries[i].in_use = true;
			strncpy(fake_entries[i].name, name, sizeof(fake_entries[i].name) - 1);
			fake_entries[i].name[sizeof(fake_entries[i].name) - 1] = '\0';
			zassert_true(val_len <= sizeof(fake_entries[i].value));
			memcpy(fake_entries[i].value, value, val_len);
			fake_entries[i].value_len = val_len;
			strncpy(last_saved_name, name, sizeof(last_saved_name) - 1);
			last_saved_name[sizeof(last_saved_name) - 1] = '\0';
			return 0;
		}
	}

	return -ENOMEM;
}

int settings_delete(const char* name)
{
	for (size_t i = 0; i < ARRAY_SIZE(fake_entries); i++) {
		if (fake_entries[i].in_use && strcmp(fake_entries[i].name, name) == 0) {
			fake_entries[i].in_use = false;
		}
	}

	strncpy(last_deleted_name, name, sizeof(last_deleted_name) - 1);
	last_deleted_name[sizeof(last_deleted_name) - 1] = '\0';
	return 0;
}

int settings_load_subtree_direct(const char* subtree, settings_load_direct_cb cb, void* param)
{
	size_t subtree_len = strlen(subtree);

	for (size_t i = 0; i < ARRAY_SIZE(fake_entries); i++) {
		if (!fake_entries[i].in_use ||
		    strncmp(fake_entries[i].name, subtree, subtree_len) != 0) {
			continue;
		}

		const char* key = fake_entries[i].name + subtree_len;
		if (*key == '/') {
			key++;
		} else if (*key != '\0') {
			continue;
		}

		int rc =
		    cb(key, fake_entries[i].value_len, fake_settings_read, &fake_entries[i], param);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}
