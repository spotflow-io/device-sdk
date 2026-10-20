#include "spotflow_ingest_key.h"

#include <spotflow/ingest_key.h>

#include <stddef.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_SPOTFLOW_INGEST_KEY_DYNAMIC
static const char* cached_ingest_key;

const char* spotflow_get_ingest_key(void)
{
	if (cached_ingest_key == NULL) {
		cached_ingest_key = spotflow_on_ingest_key_requested();
	}

	return cached_ingest_key;
}
#else
BUILD_ASSERT(sizeof(CONFIG_SPOTFLOW_INGEST_KEY) > 1,
	     "Spotflow ingest key must not be empty; set CONFIG_SPOTFLOW_INGEST_KEY or enable "
	     "CONFIG_SPOTFLOW_INGEST_KEY_DYNAMIC and implement spotflow_on_ingest_key_requested()");

const char* spotflow_get_ingest_key(void)
{
	return CONFIG_SPOTFLOW_INGEST_KEY;
}
#endif /* CONFIG_SPOTFLOW_INGEST_KEY_DYNAMIC */
