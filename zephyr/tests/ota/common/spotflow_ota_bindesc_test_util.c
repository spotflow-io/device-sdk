#include <string.h>

#include "spotflow_ota_bindesc_test_util.h"

static const uint8_t bindesc_magic[] = { 0x46, 0x60, 0xa4, 0x7e, 0x5a, 0x3e, 0x86, 0xb9 };

size_t spotflow_ota_test_bindesc_write_build_id(uint8_t* buffer, size_t buffer_size, size_t offset,
						const uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	const size_t record_size = sizeof(bindesc_magic) + 4 + SPOTFLOW_BUILD_ID_LENGTH + 4;

	if (buffer == NULL || build_id == NULL || offset + record_size > buffer_size) {
		return 0;
	}

	uint8_t* p = buffer + offset;

	memcpy(p, bindesc_magic, sizeof(bindesc_magic));
	p += sizeof(bindesc_magic);
	p[0] = 0xf0;
	p[1] = 0x25;
	p[2] = SPOTFLOW_BUILD_ID_LENGTH;
	p[3] = 0x00;
	memcpy(p + 4, build_id, SPOTFLOW_BUILD_ID_LENGTH);
	p += 4 + SPOTFLOW_BUILD_ID_LENGTH;
	p[0] = 0xff;
	p[1] = 0xff;
	p[2] = 0x00;
	p[3] = 0x00;

	return offset + record_size;
}
