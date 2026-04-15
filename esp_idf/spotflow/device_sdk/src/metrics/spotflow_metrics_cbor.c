#include "metrics/spotflow_metrics_cbor.h"
#include "net/spotflow_mqtt.h"
#include "logging/spotflow_log_backend.h"
#include "cbor.h"

/* CBOR Protocol Keys (aligned with cloud backend) */
#define KEY_MESSAGE_TYPE 0x00 /* 0 */
#define KEY_LABELS 0x05 /* 5 */
#define KEY_DEVICE_UPTIME_MS 0x06 /* 6 */
#define KEY_SEQUENCE_NUMBER 0x0D /* 13 */
#define KEY_METRIC_NAME 0x15 /* 21 */
#define KEY_AGGREGATION_INTERVAL 0x16 /* 22 */
#define KEY_SUM 0x18 /* 24 */
#define KEY_SUM_TRUNCATED 0x19 /* 25 */
#define KEY_COUNT 0x1A /* 26 */
#define KEY_MIN 0x1B /* 27 */
#define KEY_MAX 0x1C /* 28 */
#define KEY_SAMPLES 0x1D /* 29 - reserved for future */

/* Message Type */
#define METRIC_MESSAGE_TYPE 0x05

/* Internal helper functions */
static bool encode_labels(CborEncoder* map, const struct metric_label_storage* labels,
                          uint8_t label_count);
static bool encode_metric_header(CborEncoder* map, struct spotflow_metric_base* metric,
                                 int64_t timestamp_ms, uint64_t sequence_number);
static bool encode_aggregation_stats(CborEncoder* map, struct spotflow_metric_base* metric,
                                     struct metric_timeseries_state* ts);

#define CBOR_CHECK(expr)        \
    do {                        \
        if ((err = (expr)) != CborNoError) \
            goto fail;          \
    } while (0)

/**
 * @brief Debugging Function not to be used in Production
 *
 * @param buf
 * @param len
 */
static void print_cbor_hex(const uint8_t* buf, size_t len)
{
	SPOTFLOW_LOG("CBOR buffer (%zu bytes):\n", len);
	for (size_t i = 0; i < len; i++) {
		printf("%02X ", buf[i]); // print each byte as 2-digit hex
		if ((i + 1) % 16 == 0){} // 16 bytes per line
			// SPOTFLOW_LOG("\n");
	}
	SPOTFLOW_LOG("\n");
}
int spotflow_metrics_cbor_encode_aggregated(struct spotflow_metric_base* metric,
					    struct metric_timeseries_state* ts,
					    int64_t timestamp_ms, uint64_t sequence_number,
					    uint8_t** cbor_data, size_t* cbor_len)
{
	if (metric == NULL || ts == NULL || cbor_data == NULL || cbor_len == NULL) {
		return -EINVAL;
	}

	if (metric->agg_interval == SPOTFLOW_AGG_INTERVAL_NONE) {
		SPOTFLOW_LOG("This function should not be used for non-aggregated metrics");
		return -EINVAL;
	}

	uint8_t* buffer = malloc(CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE);
	if (!buffer) {
		SPOTFLOW_LOG("Failed to allocate CBOR encoding buffer");
		return -ENOMEM;
	}

	CborEncoder encoder, map;
    cbor_encoder_init(&encoder, buffer, CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE, 0);

	/* Calculate actual map entry count (dynamic map size) */
	/* Base entries: messageType, metricName, aggregationInterval, deviceUptimeMs,
	 *               sequenceNumber, sum, count, min, max = 9 */
	uint32_t map_entries = 9;
	if (ts->label_count > 0) {
		map_entries++; /* labels */
	}
	if (ts->sum_truncated) {
		map_entries++; /* sumTruncated */
	}

	/* Start CBOR map with exact entry count */
	if (cbor_encoder_create_map(&encoder, &map, map_entries) != CborNoError) {
        free(buffer);
        return -EINVAL; 
    }

	if (!encode_metric_header(&map, metric, timestamp_ms, sequence_number) ||
        (ts->label_count > 0 && !encode_labels(&map, ts->labels, ts->label_count)) ||
        !encode_aggregation_stats(&map, metric, ts))
    {
        SPOTFLOW_LOG("CBOR encoding failed");
        free(buffer);
        return -EINVAL;
    }

	if (cbor_encoder_close_container(&encoder, &map) != CborNoError) {
        free(buffer);
        return -EINVAL;
    }

	size_t len = cbor_encoder_get_buffer_size(&encoder, buffer);
    *cbor_data = malloc(len);
    if (!*cbor_data) {
        free(buffer);
        return -12;
    }
    memcpy(*cbor_data, buffer, len);
    *cbor_len = len;
    free(buffer);
    print_cbor_hex(*cbor_data,*cbor_len);
    SPOTFLOW_DEBUG("\nEncoded aggregated metric '%s' (%zu bytes, seq=%" PRIu64 ")",
             metric->name, len, sequence_number);
    return 0;
}

int spotflow_metrics_cbor_encode_no_aggregation(struct spotflow_metric_base* metric,
						const struct spotflow_label* labels,
						uint8_t label_count, int64_t value_int,
						float value_float, int64_t timestamp_ms,
						uint64_t sequence_number, uint8_t** cbor_data,
						size_t* cbor_len)
{
	if (metric == NULL || cbor_data == NULL || cbor_len == NULL) {
		return -EINVAL;
	}

	if (metric->agg_interval != SPOTFLOW_AGG_INTERVAL_NONE) {
		SPOTFLOW_LOG("This function should not be used for aggregated metrics");
		return -EINVAL; /* This function is for non-aggregated metrics only */
	}

	uint8_t* buffer = malloc(CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE);
	if (!buffer) {
		SPOTFLOW_LOG("Failed to allocate CBOR encoding buffer");
		return -ENOMEM;
	}

	CborEncoder encoder, map;
    cbor_encoder_init(&encoder, buffer, CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE, 0);

	int map_entries = 6 + (label_count ? 1 : 0);
    if (cbor_encoder_create_map(&encoder, &map, map_entries) != CborNoError) {
        free(buffer);
        return -EINVAL;
    }

    if (!encode_metric_header(&map, metric, timestamp_ms, sequence_number)) {
        free(buffer);
        return -EINVAL;
    }

    /* Labels */
    if (label_count > 0) {
        CborEncoder labels_map;
        if (cbor_encode_uint(&map, KEY_LABELS) != CborNoError ||
            cbor_encoder_create_map(&map, &labels_map, label_count) != CborNoError) {
            free(buffer);
            return -EINVAL;
        }

        for (uint8_t i = 0; i < label_count; i++) {
            if (cbor_encode_text_stringz(&labels_map, labels[i].key) != CborNoError ||
                cbor_encode_text_stringz(&labels_map, labels[i].value) != CborNoError)
            {
                free(buffer);
                return -EINVAL;
            }
        }
        if (cbor_encoder_close_container(&map, &labels_map) != CborNoError) {
            free(buffer);
            return -EINVAL;
        }
    }

    /* Value */
    if (cbor_encode_uint(&map, KEY_SUM) != CborNoError) {
        free(buffer);
        return -EINVAL;
    }

    if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
        if (cbor_encode_double(&map, value_float) != CborNoError) {
            free(buffer);
            return -EINVAL;
        }
    } else if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
        if (cbor_encode_int(&map, value_int) != CborNoError) {
            free(buffer);
            return -EINVAL;
        }
    } else {
        free(buffer);
        return -EINVAL;
    }

    if (cbor_encoder_close_container(&encoder, &map) != CborNoError) {
        free(buffer);
        return -EINVAL;
    }

    size_t len = cbor_encoder_get_buffer_size(&encoder, buffer);
    *cbor_data = malloc(len);
    if (!*cbor_data) {
        free(buffer);
        return -ENOMEM;
    }
    memcpy(*cbor_data, buffer, len);
    *cbor_len = len;
    free(buffer);

    print_cbor_hex(*cbor_data,*cbor_len);
	SPOTFLOW_DEBUG("\nEncoded raw metric '%s' message (%zu bytes, seq=%" PRIu64 ")", metric->name,
		*cbor_len, sequence_number);

	return 0;
}

int spotflow_metrics_cbor_encode_heartbeat(int64_t uptime_ms, uint8_t** data, size_t* len)
{
    uint8_t buffer[64];
    CborEncoder encoder, map;
    CborError err;
    cbor_encoder_init(&encoder, buffer, sizeof(buffer), 0);

    CBOR_CHECK(cbor_encoder_create_map(&encoder, &map, 4)); 

    CBOR_CHECK(cbor_encode_uint(&map, KEY_MESSAGE_TYPE));
    CBOR_CHECK(cbor_encode_uint(&map, METRIC_MESSAGE_TYPE));
    CBOR_CHECK(cbor_encode_uint(&map, KEY_METRIC_NAME));
    CBOR_CHECK(cbor_encode_text_stringz(&map, "uptime_ms"));
    CBOR_CHECK(cbor_encode_uint(&map, KEY_DEVICE_UPTIME_MS));
    CBOR_CHECK(cbor_encode_int(&map, uptime_ms));
    CBOR_CHECK(cbor_encode_uint(&map, KEY_SUM));
    CBOR_CHECK(cbor_encode_int(&map, uptime_ms));
    CBOR_CHECK(cbor_encoder_close_container(&encoder, &map));

    size_t encoded_len = cbor_encoder_get_buffer_size(&encoder, buffer);
    *data = malloc(encoded_len);
    if (!*data) return -12;

    memcpy(*data, buffer, encoded_len);
    *len = encoded_len;

    print_cbor_hex(*data,*len);
    return 0;
fail:
    return err;
}

/**
 * @brief Encode labels as CBOR map
 */
static bool encode_labels(CborEncoder* map, const struct metric_label_storage* labels,
                          uint8_t label_count)
{
    CborEncoder labels_map;
    CborError err;

    CBOR_CHECK(cbor_encode_uint(map, KEY_LABELS));
    CBOR_CHECK(cbor_encoder_create_map(map, &labels_map, label_count));

    for (uint8_t i = 0; i < label_count; i++) {
        CBOR_CHECK(cbor_encode_text_stringz(&labels_map, labels[i].key));
        CBOR_CHECK(cbor_encode_text_stringz(&labels_map, labels[i].value));
    }
    CBOR_CHECK(cbor_encoder_close_container(map, &labels_map));
    
    return true;

fail:
    return false;
}

static bool encode_metric_header(CborEncoder* map, struct spotflow_metric_base* metric,
                                 int64_t timestamp_ms, uint64_t sequence_number)
{
    CborError err;
    CBOR_CHECK(cbor_encode_uint(map, KEY_MESSAGE_TYPE));
    CBOR_CHECK(cbor_encode_uint(map, METRIC_MESSAGE_TYPE));
    CBOR_CHECK(cbor_encode_uint(map, KEY_METRIC_NAME));
    CBOR_CHECK(cbor_encode_text_stringz(map, metric->name));
    CBOR_CHECK(cbor_encode_uint(map, KEY_AGGREGATION_INTERVAL));
    CBOR_CHECK(cbor_encode_uint(map, metric->agg_interval));
    CBOR_CHECK(cbor_encode_uint(map, KEY_DEVICE_UPTIME_MS));
    CBOR_CHECK(cbor_encode_int(map, timestamp_ms));
    CBOR_CHECK(cbor_encode_uint(map, KEY_SEQUENCE_NUMBER));
    CBOR_CHECK(cbor_encode_uint(map, sequence_number));

    return true;
fail:
    return false;
}

static bool encode_aggregation_stats(CborEncoder* map, struct spotflow_metric_base* metric,
                                     struct metric_timeseries_state* ts)
{
    CborError err;

    CBOR_CHECK(cbor_encode_uint(map, KEY_SUM));
    if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
        CBOR_CHECK(cbor_encode_double(map, ts->sum_float));
    } else if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
        CBOR_CHECK(cbor_encode_int(map, ts->sum_int));
    }

    if (ts->sum_truncated) {
        CBOR_CHECK(cbor_encode_uint(map, KEY_SUM_TRUNCATED));
        CBOR_CHECK(cbor_encode_boolean(map, true));
    }

    CBOR_CHECK(cbor_encode_uint(map, KEY_COUNT));
    CBOR_CHECK(cbor_encode_uint(map, ts->count));

    CBOR_CHECK(cbor_encode_uint(map, KEY_MIN));

    if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
        CBOR_CHECK(cbor_encode_double(map, ts->min_float));
    } else if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
        CBOR_CHECK(cbor_encode_int(map, ts->min_int));
    }

    CBOR_CHECK(cbor_encode_uint(map, KEY_MAX));
    if (metric->type == SPOTFLOW_METRIC_TYPE_FLOAT) {
        CBOR_CHECK(cbor_encode_double(map, ts->max_float));
    } else if (metric->type == SPOTFLOW_METRIC_TYPE_INT) {
        CBOR_CHECK(cbor_encode_int(map, ts->max_int));
    }

    return true;

fail:
    return false;
}