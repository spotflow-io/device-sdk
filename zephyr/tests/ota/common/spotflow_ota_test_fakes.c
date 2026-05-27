#include "spotflow_ota_test_fakes.h"

static struct spotflow_ota_test_fake_mqtt fake_mqtt;

void spotflow_ota_test_fakes_reset(void)
{
	fake_mqtt = (struct spotflow_ota_test_fake_mqtt){ 0 };
}

struct spotflow_ota_test_fake_mqtt *spotflow_ota_test_fake_mqtt_get(void)
{
	return &fake_mqtt;
}
