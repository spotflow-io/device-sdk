#include <string.h>

#include "spotflow_ota_test_fakes.h"

static struct spotflow_ota_test_fake_mqtt fake_mqtt;
static struct spotflow_ota_test_fake_callbacks fake_callbacks;

void spotflow_ota_test_fakes_reset(void)
{
	fake_mqtt = (struct spotflow_ota_test_fake_mqtt){ 0 };
	fake_callbacks = (struct spotflow_ota_test_fake_callbacks){
		.next_handle_result = SPOTFLOW_OTA_RESULT_SUCCEEDED,
	};
	k_sem_init(&fake_callbacks.handle_called_sem, 0, 1);
	k_sem_init(&fake_callbacks.handle_continue_sem, 0, 1);
	k_sem_init(&fake_callbacks.cancel_called_sem, 0, 1);
}

struct spotflow_ota_test_fake_mqtt* spotflow_ota_test_fake_mqtt_get(void)
{
	return &fake_mqtt;
}

struct spotflow_ota_test_fake_callbacks* spotflow_ota_test_fake_callbacks_get(void)
{
	return &fake_callbacks;
}
