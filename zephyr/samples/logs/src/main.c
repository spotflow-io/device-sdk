#include <zephyr/bindesc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <spotflow/session_metadata.h>

#include "net.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_INF);

/* Uncomment this function to provide your own device ID in runtime */
/*const char* spotflow_override_device_id()
{
	return "my_nrf7002dk_test";
}*/

/* Uncomment this callback to attach labels to all telemetry in a session. */
/*static const struct spotflow_session_label session_labels[] = {
	{
		.key = "hardwareRevision",
		.type = SPOTFLOW_SESSION_LABEL_STRING,
		.value.string = "rev-b",
	},
	{
		.key = "productionLine",
		.type = SPOTFLOW_SESSION_LABEL_INT,
		.value.integer = 4,
	},
};

struct spotflow_session_metadata_labels spotflow_override_session_metadata_labels(void)
{
	return (struct spotflow_session_metadata_labels){
		.items = session_labels,
		.count = ARRAY_SIZE(session_labels),
	};
}*/

int main(void)
{
	LOG_INF("Starting Spotflow logging example");

	// Wait for the initialization of network device
	k_sleep(K_SECONDS(1));

	spotflow_sample_net_init();

	for (int i = 0; i < 20; i++) {
		LOG_INF("Hello from Zephyr to Spotflow: %d", i);
		k_sleep(K_SECONDS(2));
	}

	return 0;
}
