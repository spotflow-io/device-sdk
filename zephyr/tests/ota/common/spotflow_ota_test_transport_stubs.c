#include <errno.h>
#include <string.h>

#include "net/spotflow_transport.h"

#include "spotflow_ota_test_fakes.h"

int spotflow_transport_send_ota_cbor(uint8_t* payload, size_t len)
{
	struct spotflow_ota_test_fake_transport* fake = spotflow_ota_test_fake_transport_get();

	if (len > sizeof(fake->payload)) {
		return -ENOMEM;
	}

	fake->publish_count++;
	memcpy(fake->payload, payload, len);
	fake->last_payload = fake->payload;
	fake->last_payload_len = len;

	return fake->publish_result;
}

int spotflow_transport_subscribe_ota(spotflow_transport_message_cb callback)
{
	struct spotflow_ota_test_fake_transport* fake = spotflow_ota_test_fake_transport_get();

	fake->ota_callback = callback;
	fake->ota_subscribe_count++;

	return fake->ota_subscribe_result;
}

int spotflow_transport_send_ingest_cbor(uint8_t* payload, size_t len)
{
	struct spotflow_ota_test_fake_transport* fake = spotflow_ota_test_fake_transport_get();

	if (len > sizeof(fake->ingest_payload)) {
		return -ENOMEM;
	}

	memcpy(fake->ingest_payload, payload, len);
	fake->ingest_payload_len = len;

	return 0;
}
