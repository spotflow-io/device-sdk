#include "test_common.h"

bool contains_cbor_key(const uint8_t* cbor_data, size_t cbor_len, uint32_t key,
			      uint32_t expected_value)
{
	bool result = false;
	CborParser parser;
	CborValue map;
	CborValue element;
	CborError err;

	/* MISRA C: Check for NULL pointer */
	if (cbor_data == NULL) {
		return false;
	}

	/* MISRA C: Check for zero length */
	if (cbor_len == 0U) {
		return false;
	}

	/* Initialize CBOR parser */
	err = cbor_parser_init(cbor_data, cbor_len, 0U, &parser, &map);
	if (err != CborNoError) {
		return false;
	}

	/* Verify that the top-level element is a map */
	if (!cbor_value_is_map(&map)) {
		return false;
	}

	/* Enter the map */
	err = cbor_value_enter_container(&map, &element);
	if (err != CborNoError) {
		return false;
	}

	/* Iterate through map entries */
	while (!cbor_value_at_end(&element)) {
		uint64_t current_key = 0U;
		uint64_t current_value = 0U;

		/* Read the key (must be an unsigned integer) */
		if (cbor_value_is_unsigned_integer(&element)) {
			err = cbor_value_get_uint64(&element, &current_key);
			if (err != CborNoError) {
				break;
			}

			/* Advance to the value */
			err = cbor_value_advance(&element);
			if (err != CborNoError) {
				break;
			}

			/* Check if we found the target key */
			if (current_key == (uint64_t)key) {
				/* Read the value (must be an unsigned integer) */
				if (cbor_value_is_unsigned_integer(&element)) {
					err = cbor_value_get_uint64(&element, &current_value);
					if (err != CborNoError) {
						break;
					}

					/* Compare with expected value */
					if (current_value == (uint64_t)expected_value) {
						result = true;
					}
				}
				/* Key found, exit loop regardless of value match */
				break;
			}

			/* Skip the value for non-matching keys */
			err = cbor_value_advance(&element);
			if (err != CborNoError) {
				break;
			}
		} else {
			/* Invalid key type - skip this key-value pair */
			err = cbor_value_advance(&element);
			if (err != CborNoError) {
				break;
			}
			err = cbor_value_advance(&element);
			if (err != CborNoError) {
				break;
			}
		}
	}

	/* Leave the container (not strictly necessary for verification) */
	(void)cbor_value_leave_container(&map, &element);

	return result;
}
