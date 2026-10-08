#include "net/spotflow_ingest_key.h"

#include <spotflow/ingest_key.h>

#include <string.h>
#include <zephyr/ztest.h>

#ifdef TEST_OVERRIDE
static const char* override_key;
static size_t callback_calls;

const char* spotflow_override_ingest_key(void)
{
	callback_calls++;
	return override_key;
}
#endif /* TEST_OVERRIDE */

ZTEST(spotflow_ingest_key, test_resolve_and_cache_key)
{
#if defined(TEST_SCENARIO_OVERRIDE)
	override_key = "application-key";
	const char* key = spotflow_get_ingest_key();
	zassert_equal(key, override_key);
	zassert_equal(callback_calls, 1);

	override_key = "changed-key";
	zassert_equal(spotflow_get_ingest_key(), key);
	zassert_equal(callback_calls, 1, "Cached keys must not invoke the callback again");
#elif defined(TEST_SCENARIO_FALLBACK)
	override_key = NULL;
	const char* key = spotflow_get_ingest_key();
	zassert_not_null(key);
	zassert_equal(strcmp(key, "config-key"), 0);
	zassert_equal(callback_calls, 1);

	override_key = "application-key";
	zassert_equal(spotflow_get_ingest_key(), key);
	zassert_equal(callback_calls, 1);
#elif defined(TEST_SCENARIO_MISSING)
	override_key = NULL;
	zassert_is_null(spotflow_get_ingest_key());
	zassert_is_null(spotflow_get_ingest_key());
	zassert_equal(callback_calls, 2, "Missing keys must be retried");

	override_key = "provisioned-key";
	const char* key = spotflow_get_ingest_key();
	zassert_equal(key, override_key);
	zassert_equal(callback_calls, 3);

	override_key = NULL;
	zassert_equal(spotflow_get_ingest_key(), key);
	zassert_equal(callback_calls, 3);
#elif defined(TEST_SCENARIO_EMPTY_OVERRIDE)
	override_key = "";
	const char* key = spotflow_get_ingest_key();
	zassert_not_null(key);
	zassert_equal(key, override_key, "Empty overrides must take precedence over Kconfig");
	zassert_equal(key[0], '\0');
	zassert_equal(callback_calls, 1);

	override_key = NULL;
	zassert_equal(spotflow_get_ingest_key(), key, "Empty overrides must be cached");
	zassert_equal(callback_calls, 1);

	override_key = "changed-key";
	zassert_equal(spotflow_get_ingest_key(), key);
	zassert_equal(callback_calls, 1);
#elif defined(TEST_SCENARIO_WEAK_FALLBACK)
	zassert_is_null(spotflow_override_ingest_key());
	const char* key = spotflow_get_ingest_key();
	zassert_not_null(key);
	zassert_equal(strcmp(key, "config-key"), 0);
	zassert_equal(spotflow_get_ingest_key(), key);
#elif defined(TEST_SCENARIO_WEAK_MISSING)
	zassert_is_null(spotflow_override_ingest_key());
	zassert_is_null(spotflow_get_ingest_key());
	zassert_is_null(spotflow_get_ingest_key());
#else
#error "Unknown ingest key test scenario"
#endif
}

ZTEST_SUITE(spotflow_ingest_key, NULL, NULL, NULL, NULL, NULL);
