#include "spotflow_ingest_key.h"

#include <spotflow/ingest_key.h>

#include <stddef.h>
#include <string.h>
#include <zephyr/toolchain.h>

static const char* cached_ingest_key;

const char* __weak spotflow_override_ingest_key(void)
{
	return NULL;
}

const char* spotflow_get_ingest_key(void)
{
	if (cached_ingest_key != NULL) {
		return cached_ingest_key;
	}

	const char* ingest_key = spotflow_override_ingest_key();
	if (ingest_key == NULL) {
		if (strlen(CONFIG_SPOTFLOW_INGEST_KEY) > 0) {
			ingest_key = CONFIG_SPOTFLOW_INGEST_KEY;
		} else {
			return NULL;
		}
	}

	cached_ingest_key = ingest_key;
	return cached_ingest_key;
}
