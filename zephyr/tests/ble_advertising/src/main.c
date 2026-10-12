#include <zephyr/bluetooth/gap.h>
#include <zephyr/ztest.h>

#include "net/transport/ble/spotflow_ble_transport_internal.h"

ZTEST_SUITE(spotflow_ble_advertising, NULL, NULL, NULL, NULL, NULL);

ZTEST(spotflow_ble_advertising, test_fast_stage_uses_the_fast_interval)
{
	struct spotflow_ble_adv_params params;

	spotflow_ble_adv_stage_params_get(SPOTFLOW_BLE_ADV_STAGE_FAST, &params);

	zassert_equal(params.interval_min, BT_GAP_ADV_FAST_INT_MIN_1);
	zassert_equal(params.interval_max, BT_GAP_ADV_FAST_INT_MAX_1);
#ifdef CONFIG_SPOTFLOW_BLE_ADV_BACKOFF
	zassert_equal(params.duration_s, 20, "unexpected duration %u", params.duration_s);
#else
	zassert_equal(params.duration_s, 0, "fast advertising must not end without backoff");
#endif
}

#ifdef CONFIG_SPOTFLOW_BLE_ADV_BACKOFF

ZTEST(spotflow_ble_advertising, test_slow_stage_interval_and_duration)
{
	struct spotflow_ble_adv_params params;

	spotflow_ble_adv_stage_params_get(SPOTFLOW_BLE_ADV_STAGE_SLOW, &params);

	/* 1000 ms in 0.625 ms units */
	zassert_equal(params.interval_min, 1600, "unexpected interval %u", params.interval_min);
	zassert_equal(params.interval_max, 1600, "unexpected interval %u", params.interval_max);
	zassert_equal(params.duration_s, 120, "unexpected duration %u", params.duration_s);
}

ZTEST(spotflow_ble_advertising, test_idle_stage_lasts_until_connected)
{
	struct spotflow_ble_adv_params params;

	spotflow_ble_adv_stage_params_get(SPOTFLOW_BLE_ADV_STAGE_IDLE, &params);

	/* 10.24 s, the longest legacy advertising interval: 0x4000 units */
	zassert_equal(params.interval_min, 0x4000, "unexpected interval %u", params.interval_min);
	zassert_equal(params.interval_max, 0x4000, "unexpected interval %u", params.interval_max);
	zassert_equal(params.duration_s, 0, "idle advertising must not time out");
}

ZTEST(spotflow_ble_advertising, test_stages_slow_down_and_stay_idle)
{
	zassert_equal(spotflow_ble_adv_next_stage(SPOTFLOW_BLE_ADV_STAGE_FAST),
		      SPOTFLOW_BLE_ADV_STAGE_SLOW);
	zassert_equal(spotflow_ble_adv_next_stage(SPOTFLOW_BLE_ADV_STAGE_SLOW),
		      SPOTFLOW_BLE_ADV_STAGE_IDLE);
	zassert_equal(spotflow_ble_adv_next_stage(SPOTFLOW_BLE_ADV_STAGE_IDLE),
		      SPOTFLOW_BLE_ADV_STAGE_IDLE);
}

ZTEST(spotflow_ble_advertising, test_intervals_never_get_faster)
{
	struct spotflow_ble_adv_params fast;
	struct spotflow_ble_adv_params slow;
	struct spotflow_ble_adv_params idle;

	spotflow_ble_adv_stage_params_get(SPOTFLOW_BLE_ADV_STAGE_FAST, &fast);
	spotflow_ble_adv_stage_params_get(SPOTFLOW_BLE_ADV_STAGE_SLOW, &slow);
	spotflow_ble_adv_stage_params_get(SPOTFLOW_BLE_ADV_STAGE_IDLE, &idle);

	zassert_true(fast.interval_max <= slow.interval_min);
	zassert_true(slow.interval_max <= idle.interval_min);
}

#else /* !CONFIG_SPOTFLOW_BLE_ADV_BACKOFF */

ZTEST(spotflow_ble_advertising, test_without_backoff_advertising_stays_fast)
{
	zassert_equal(spotflow_ble_adv_next_stage(SPOTFLOW_BLE_ADV_STAGE_FAST),
		      SPOTFLOW_BLE_ADV_STAGE_FAST);
	zassert_equal(spotflow_ble_adv_next_stage(SPOTFLOW_BLE_ADV_STAGE_SLOW),
		      SPOTFLOW_BLE_ADV_STAGE_FAST);
}

#endif /* CONFIG_SPOTFLOW_BLE_ADV_BACKOFF */
