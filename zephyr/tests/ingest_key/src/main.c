#include "net/spotflow_ingest_key.h"

#include <spotflow/ingest_key.h>

#include <string.h>
#include <zephyr/ztest.h>

#if defined(CONFIG_SPOTFLOW_INGEST_KEY_DYNAMIC) && !defined(TEST_SCENARIO_DYNAMIC_NO_CALLBACK)
static const char* provided_key;
static size_t callback_calls;

const char* spotflow_on_ingest_key_requested(void)
{
	callback_calls++;
	return provided_key;
}
#endif /* Dynamic mode with an application callback */

ZTEST(spotflow_ingest_key, test_resolve_and_cache_key)
{
#if defined(TEST_SCENARIO_DYNAMIC)
	provided_key = "application-key";
	const char* key = spotflow_get_ingest_key();
	zassert_equal(key, provided_key);
	zassert_equal(callback_calls, 1);

	provided_key = "changed-key";
	zassert_equal(spotflow_get_ingest_key(), key);
	zassert_equal(callback_calls, 1, "Cached keys must not invoke the callback again");
#elif defined(TEST_SCENARIO_DYNAMIC_RETRY)
	provided_key = NULL;
	zassert_is_null(spotflow_get_ingest_key());
	zassert_is_null(spotflow_get_ingest_key());
	zassert_equal(callback_calls, 2, "Missing keys must be retried");

	provided_key = "provisioned-key";
	const char* key = spotflow_get_ingest_key();
	zassert_equal(key, provided_key);
	zassert_equal(callback_calls, 3);

	provided_key = NULL;
	zassert_equal(spotflow_get_ingest_key(), key);
	zassert_equal(callback_calls, 3);
#elif defined(TEST_SCENARIO_DYNAMIC_EMPTY)
	provided_key = "";
	const char* key = spotflow_get_ingest_key();
	zassert_not_null(key);
	zassert_equal(key, provided_key, "The callback result must be used");
	zassert_equal(key[0], '\0');
	zassert_equal(callback_calls, 1);

	provided_key = NULL;
	zassert_equal(spotflow_get_ingest_key(), key, "Empty keys must be cached");
	zassert_equal(callback_calls, 1);

	provided_key = "changed-key";
	zassert_equal(spotflow_get_ingest_key(), key);
	zassert_equal(callback_calls, 1);
#elif defined(TEST_SCENARIO_DYNAMIC_NO_CALLBACK) || defined(TEST_SCENARIO_STATIC_MISSING)
	/* These scenarios must fail to build before reaching the test. */
	zassert_not_null(spotflow_get_ingest_key());
#elif defined(TEST_SCENARIO_STATIC)
	const char* key = spotflow_get_ingest_key();
	zassert_not_null(key);
	zassert_equal(strcmp(key, "config-key"), 0);
	zassert_equal(spotflow_get_ingest_key(), key);
#else
#error "Unknown ingest key test scenario"
#endif
}

ZTEST_SUITE(spotflow_ingest_key, NULL, NULL, NULL, NULL, NULL);
