#include "spotflow_log_backend.h"

#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/kernel.h>

#include "logging/spotflow_log_cbor.h"
#include "logging/spotflow_cbor_output_context.h"
#include "net/spotflow_config.h"
#include "net/spotflow_processor.h"

LOG_MODULE_REGISTER(spotflow_logging, CONFIG_SPOTFLOW_LOGS_PROCESSING_LOG_LEVEL);

/* https://docs.zephyrproject.org/apidoc/latest/group__msgq__apis.html
 * Alignment of the message queue's ring buffer is not necessary,
 * setting q_align to 1 is sufficient.*/
K_MSGQ_DEFINE(g_spotflow_logs_msgq, sizeof(struct spotflow_mqtt_logs_msg*),
	      CONFIG_SPOTFLOW_LOG_BACKEND_QUEUE_SIZE, 1);

struct spotflow_log_context {
	struct spotflow_cbor_output_context* cbor_output_context;
	size_t dropped_backend_count;
	size_t message_index;
};

static struct spotflow_log_context spotflow_log_ctx;

static void init(const struct log_backend* backend);

static void process(const struct log_backend* backend, union log_msg_generic* msg);

static void panic(const struct log_backend* backend);

static void dropped(const struct log_backend* backend, uint32_t cnt);

static const struct log_backend_api log_backend_spotflow_api = {
	.init = init,
	.process = process,
	.panic = panic,
	.dropped = dropped,
	/* .sync_string and .sync_hexdump can be added if you use IMMEDIATE mode */
};

#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
/* can be autostarted because there is message queue where messages are stored */
LOG_BACKEND_DEFINE(log_backend_spotflow, log_backend_spotflow_api, true /* autostart */,
		   &spotflow_log_ctx);
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND */

static int enqueue_log_msg(const struct spotflow_mqtt_logs_msg* msg,
			   struct spotflow_log_context* ctx);

static void process_single_message_stats_update(struct spotflow_log_context* context, bool dropped);

static void process_message_stats_update(struct spotflow_log_context* context, uint32_t cnt,
					 bool dropped);

static void init(const struct log_backend* const backend)
{
	LOG_DBG("Initializing spotflow logging backend");

	__ASSERT(backend->cb->ctx != NULL, "Spotflow log backend context is NULL");
	struct spotflow_log_context* ctx = backend->cb->ctx;
	int rc = spotflow_cbor_output_context_init(&ctx->cbor_output_context);
	if (rc < 0) {
		LOG_ERR("Failed to initialize spotflow output context: %d", rc);
		return;
	}
	ctx->dropped_backend_count = 0;
	ctx->message_index = 0;

	spotflow_start_mqtt();

	LOG_INF("Spotflow logging backend initialized.");
}

static void process(const struct log_backend* const backend, union log_msg_generic* msg)
{
	struct log_msg* log_msg = &msg->log;
	struct spotflow_log_context* ctx = backend->cb->ctx;
	if (ctx == NULL) {
		LOG_DBG("No spotflow log context");
		return;
	}

	if (log_msg_get_level(log_msg) > spotflow_config_get_sent_log_level()) {
		return;
	}

	uint8_t* cbor_data = NULL;
	size_t cbor_data_len = 0;
	int rc = spotflow_cbor_encode_log(log_msg, ctx->message_index, ctx->cbor_output_context,
					  &cbor_data, &cbor_data_len);

	if (rc < 0) {
		LOG_DBG("Failed to encode message: %d", rc);
		return;
	}

	/* Allocate memory for the message structure */
	struct spotflow_mqtt_logs_msg* mqtt_msg = k_malloc(sizeof(struct spotflow_mqtt_logs_msg));
	if (!mqtt_msg) {
		LOG_DBG("Failed to allocate memory for message");
		k_free(cbor_data);
		return;
	}

	/* Set up the message */
	mqtt_msg->payload = cbor_data;
	mqtt_msg->len = cbor_data_len;

	/* Enqueue the message (passing pointer) */
	if (enqueue_log_msg(mqtt_msg, ctx) < 0) {
		LOG_DBG("Unable to put message in queue, dropping");
		k_free(mqtt_msg->payload);
		k_free(mqtt_msg);
		process_single_message_stats_update(ctx, true /* dropped */);
	} else {
		process_single_message_stats_update(ctx, false /* dropped */);
	}
}

static void panic(const struct log_backend* const backend)
{
	ARG_UNUSED(backend);
}

static void dropped(const struct log_backend* const backend, uint32_t cnt)
{
	/* Message did not reached the process function, dropping by zephyr middleware. */
	/* Currently, we do not distinguish between backend and middleware drops. */
	struct spotflow_log_context* ctx = backend->cb->ctx;
	process_message_stats_update(ctx, cnt, true /* dropped */);
}

static int drop_log_msg_from_queue(struct spotflow_log_context* ctx);

/* Helper that will drop & free the oldest if queue is full */
static int enqueue_log_msg(const struct spotflow_mqtt_logs_msg* msg,
			   struct spotflow_log_context* ctx)
{
	int rc = k_msgq_put(&g_spotflow_logs_msgq, &msg, K_NO_WAIT);
	if (rc == -ENOMSG) {
		/* queue full: grab the oldest pointer, free it */
		rc = drop_log_msg_from_queue(ctx);
		if (rc == 0) {
			/* now there should be room for the new one */
			rc = k_msgq_put(&g_spotflow_logs_msgq, &msg, K_NO_WAIT);
		} else {
			LOG_DBG("Failed to get message from queue %d", rc);
		}
	}
	return rc;
}

static int drop_log_msg_from_queue(struct spotflow_log_context* ctx)
{
	char* old;
	int rc = k_msgq_get(&g_spotflow_logs_msgq, &old, K_NO_WAIT);
	if (rc == 0) {
		struct spotflow_mqtt_logs_msg* old_msg = (struct spotflow_mqtt_logs_msg*)old;
		k_free(old_msg->payload);
		k_free(old_msg);

		/* not optimal, in edge case dropped_backend_count could overflow
		but it is unlikely because message_index was already increased when added to buffer,
		only statistic, keeping it as is */
		ctx->dropped_backend_count++;
		/* currently not logged because it is messing up the output significantly */
		/*LOG_DBG("Dropped oldest message");*/
	}
	return rc;
}

static inline void print_stat(const struct spotflow_log_context* context)
{
	LOG_INF("Total processed %" PRIu32 ", dropped %" PRIu32 " messages", context->message_index,
		context->dropped_backend_count);
}

static inline void reset_stat(struct spotflow_log_context* context)
{
	context->message_index = 0;
	context->dropped_backend_count = 0;
}

static void process_single_message_stats_update(struct spotflow_log_context* context, bool dropped)
{
	process_message_stats_update(context, 1, dropped);
}

static void process_message_stats_update(struct spotflow_log_context* context, uint32_t cnt,
					 bool dropped)
{
	for (int i = 0; i < cnt; i++) {
		context->message_index++;
		if (dropped) {
			context->dropped_backend_count++;
		}

		if (context->message_index % 100 == 0) {
			print_stat(context);
		}

		if (context->message_index == SIZE_MAX) {
			print_stat(context);
			reset_stat(context);
			LOG_INF("Messages counter reset");
		}
	}
}
