#include <spotflow/session_metadata.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/util.h>

#include <stdio.h>

#define HARDWARE_ID_MAX_SIZE 16

static char runtime_device_id[sizeof("showcase-") + (2 * HARDWARE_ID_MAX_SIZE)];

const char* spotflow_override_device_id(void)
{
	uint8_t hardware_id[HARDWARE_ID_MAX_SIZE];
	ssize_t hardware_id_size = hwinfo_get_device_id(hardware_id, ARRAY_SIZE(hardware_id));

	if (hardware_id_size <= 0) {
		return NULL;
	}

	(void)snprintf(runtime_device_id, sizeof(runtime_device_id), "showcase-");
	for (size_t i = 0; i < (size_t)hardware_id_size; ++i) {
		size_t offset = sizeof("showcase-") - 1 + (2 * i);

		(void)snprintf(&runtime_device_id[offset], sizeof(runtime_device_id) - offset,
			       "%02X", hardware_id[i]);
	}

	return runtime_device_id;
}

static const struct spotflow_session_label session_labels[] = {
	{
		.key = "sample",
		.type = SPOTFLOW_SESSION_LABEL_STRING,
		.value.string = "showcase",
	},
	{
		.key = "hardwareRevision",
		.type = SPOTFLOW_SESSION_LABEL_INT,
		.value.integer = 1,
	},
	{
		.key = "samplingRatio",
		.type = SPOTFLOW_SESSION_LABEL_FLOAT,
		.value.floating = 0.25,
	},
	{
		.key = "diagnosticsEnabled",
		.type = SPOTFLOW_SESSION_LABEL_BOOL,
		.value.boolean = true,
	},
};

struct spotflow_session_metadata_labels spotflow_override_session_metadata_labels(void)
{
	return (struct spotflow_session_metadata_labels){
		.items = session_labels,
		.count = ARRAY_SIZE(session_labels),
	};
}
