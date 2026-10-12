#include "spotflow_ble_transport_internal.h"

#include <zephyr/bluetooth/gap.h>
#include <zephyr/sys/util.h>

/* Advertising intervals are expressed in 0.625 ms units. */
#define SPOTFLOW_BLE_ADV_INTERVAL_UNITS(ms) ((uint16_t)(((ms) * 8U) / 5U))

#ifdef CONFIG_SPOTFLOW_BLE_ADV_BACKOFF
BUILD_ASSERT(CONFIG_SPOTFLOW_BLE_ADV_SLOW_INTERVAL_MS <= CONFIG_SPOTFLOW_BLE_ADV_IDLE_INTERVAL_MS,
	     "the idle advertising interval must not be shorter than the slow one");
#endif

void spotflow_ble_adv_stage_params_get(enum spotflow_ble_adv_stage stage,
				       struct spotflow_ble_adv_params* params)
{
	switch (stage) {
#ifdef CONFIG_SPOTFLOW_BLE_ADV_BACKOFF
	case SPOTFLOW_BLE_ADV_STAGE_SLOW:
		params->interval_min =
			SPOTFLOW_BLE_ADV_INTERVAL_UNITS(CONFIG_SPOTFLOW_BLE_ADV_SLOW_INTERVAL_MS);
		params->interval_max = params->interval_min;
		params->duration_s = CONFIG_SPOTFLOW_BLE_ADV_SLOW_DURATION;
		return;
	case SPOTFLOW_BLE_ADV_STAGE_IDLE:
		params->interval_min =
			SPOTFLOW_BLE_ADV_INTERVAL_UNITS(CONFIG_SPOTFLOW_BLE_ADV_IDLE_INTERVAL_MS);
		params->interval_max = params->interval_min;
		params->duration_s = 0;
		return;
#endif /* CONFIG_SPOTFLOW_BLE_ADV_BACKOFF */
	case SPOTFLOW_BLE_ADV_STAGE_FAST:
	default:
		params->interval_min = BT_GAP_ADV_FAST_INT_MIN_1;
		params->interval_max = BT_GAP_ADV_FAST_INT_MAX_1;
#ifdef CONFIG_SPOTFLOW_BLE_ADV_BACKOFF
		params->duration_s = CONFIG_SPOTFLOW_BLE_ADV_FAST_DURATION;
#else
		params->duration_s = 0;
#endif
		return;
	}
}

enum spotflow_ble_adv_stage spotflow_ble_adv_next_stage(enum spotflow_ble_adv_stage stage)
{
	if (!IS_ENABLED(CONFIG_SPOTFLOW_BLE_ADV_BACKOFF)) {
		return SPOTFLOW_BLE_ADV_STAGE_FAST;
	}

	switch (stage) {
	case SPOTFLOW_BLE_ADV_STAGE_FAST:
		return SPOTFLOW_BLE_ADV_STAGE_SLOW;
	case SPOTFLOW_BLE_ADV_STAGE_SLOW:
	case SPOTFLOW_BLE_ADV_STAGE_IDLE:
	default:
		return SPOTFLOW_BLE_ADV_STAGE_IDLE;
	}
}
