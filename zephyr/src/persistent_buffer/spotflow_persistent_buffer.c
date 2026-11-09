#include "persistent_buffer/spotflow_persistent_buffer.h"

#include "net/spotflow_session_metadata.h"
#include "net/spotflow_transport.h"
#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
#include "logging/spotflow_log_backend.h"
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND */
#ifdef CONFIG_SPOTFLOW_METRICS
#include "metrics/spotflow_metrics_aggregator.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_registry.h"
#include "metrics/spotflow_metrics_types.h"
#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
#include "metrics/spotflow_metrics_cbor.h"
#include "metrics/spotflow_metrics_heartbeat.h"
#endif /* CONFIG_SPOTFLOW_METRICS_HEARTBEAT */
#endif /* CONFIG_SPOTFLOW_METRICS */
#include <spotflow/persistent_buffer.h>

#include <errno.h>
#include <string.h>

#include <zephyr/fs/fcb.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(spotflow_persistent_buffer, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#if defined(CONFIG_PARTITION_MANAGER_ENABLED)
#if !FIXED_PARTITION_EXISTS(spotflow_buffer)
#error "CONFIG_SPOTFLOW_PERSISTENT_BUFFER requires a spotflow_buffer partition in the Partition Manager configuration"
#endif
#define PB_PARTITION_ID FIXED_PARTITION_ID(spotflow_buffer)
#else
#if !FIXED_PARTITION_EXISTS(spotflow_buffer_partition)
#error "CONFIG_SPOTFLOW_PERSISTENT_BUFFER requires a devicetree partition labelled spotflow_buffer_partition"
#endif
#ifdef PARTITION_ID
#define PB_PARTITION_ID PARTITION_ID(spotflow_buffer_partition)
#else
#define PB_PARTITION_ID FIXED_PARTITION_ID(spotflow_buffer_partition)
#endif /* PARTITION_ID */
#endif /* CONFIG_PARTITION_MANAGER_ENABLED */

/* "SFPB" */
#define PB_MAGIC 0x53465042
#define PB_MAX_SECTORS 64
/* FCB writes its 8-byte sector header in a single flash write. */
#define PB_MAX_WRITE_ALIGN 8
#define PB_BATCH_MAX_MSGS 32
/* Size of FCB's on-flash sector header (struct fcb_disk_area). */
#define PB_FCB_SECTOR_HEADER_LEN 8
#define PB_FCB_CRC_LEN 1
#define PB_SESSION_RECORD_MAX_LEN (SPOTFLOW_PB_RUN_ID_LEN + CONFIG_SPOTFLOW_SESSION_METADATA_BUFFER_SIZE)
#define PB_DROPPED_METRIC_NAME "spotflow_buffer_dropped_messages"

enum pb_msg_source {
	PB_SOURCE_STATIC,
	PB_SOURCE_LOG,
	PB_SOURCE_METRIC,
};

struct pb_msg {
	uint8_t kind;
	uint8_t source;
	uint16_t len;
	const uint8_t* payload;
	void* owner;
};

/* Position of the sender in the ring. */
struct pb_reader {
	/* Entry being sent, or the last entry sent when in_entry is false. A NULL sector means
	 * reading starts at the oldest entry. */
	struct fcb_entry loc;
	bool in_entry;
	/* Offset of the next message within the entry's data. */
	uint16_t msg_off;
	/* Run that the following messages belong to, from the last session record read. */
	bool run_known;
	uint64_t run_id;
	uint16_t session_len;
	/* Incremented whenever the position is invalidated by dropping or clearing data. */
	uint32_t epoch;
};

/* Writes a byte stream to flash in units of the flash write block size. */
struct pb_writer {
	struct flash_sector* sector;
	uint32_t off;
	uint8_t align;
	uint8_t carry_len;
	uint8_t carry[PB_MAX_WRITE_ALIGN];
	int rc;
};

struct pb_context {
	atomic_t state;
	struct fcb fcb;
	struct flash_sector sectors[PB_MAX_SECTORS];
	uint32_t sector_cnt;
	size_t total_size;
	size_t max_entry_len;
	uint32_t first_elem_off;

	/* Persister state */
	bool session_written;
	uint8_t session_record[PB_SESSION_RECORD_MAX_LEN];
	uint16_t session_record_len;
	struct pb_msg batch[PB_BATCH_MAX_MSGS];
	struct pb_msg carry;
	bool carry_valid;
#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
	uint8_t heartbeat[SPOTFLOW_METRICS_HEARTBEAT_CBOR_MAX_LEN];
#endif /* CONFIG_SPOTFLOW_METRICS_HEARTBEAT */

	/* Sender state */
	struct pb_reader rd;
	uint8_t rd_session[PB_SESSION_RECORD_MAX_LEN];
	uint64_t last_sent_run;
	atomic_t session_restarted;

	/* Statistics */
	uint32_t dropped_total;
	atomic_t dropped_unreported;
};

static struct pb_context pb;
/* Protects the ring and all the state above. Statically initialized so that the public API can
 * be called before the buffer is initialized. */
static K_MUTEX_DEFINE(pb_lock);

static int check_write_block_size(void);
static int init_fcb(void);
static bool format_matches(void);
static int erase_partition(void);
static void reset_reader(void);
static int encode_session_record(void);
static size_t collect_batch(size_t cap, size_t* data_len);
static bool add_to_batch(struct pb_msg* msg, size_t* n, size_t* total, size_t cap);
static void release_msg(struct pb_msg* msg);
static int write_batch(size_t n, size_t data_len);
static bool needs_session_record(size_t data_len);
static size_t entry_flash_size(size_t data_len);
static uint32_t align_up(uint32_t len);
static void writer_init(struct pb_writer* w, const struct fcb_entry* loc);
static void writer_put(struct pb_writer* w, const uint8_t* data, size_t len);
static void writer_finish(struct pb_writer* w);
static int drop_oldest_sector(void);
static void release_sent_sectors(const struct flash_sector* current);
static int next_message(uint16_t* len);
static int read_msg_header(const struct fcb_entry* loc, uint16_t off, uint8_t* kind,
			   uint16_t* len);
static uint32_t count_messages(const struct fcb_entry* loc, uint16_t from_off);
static uint32_t count_unsent_in_sector(const struct flash_sector* sector);
static struct flash_sector* next_sector(struct flash_sector* sector);
static void add_dropped(uint32_t count);
static void report_dropped(void);

int spotflow_pb_init(void)
{
	int rc;
	uint32_t sector_cnt = ARRAY_SIZE(pb.sectors);

	k_mutex_lock(&pb_lock, K_FOREVER);

	rc = flash_area_get_sectors(PB_PARTITION_ID, &sector_cnt, pb.sectors);
	if (rc == -ENOMEM) {
		LOG_ERR("Persistent buffer partition has more than %d sectors", PB_MAX_SECTORS);
		goto fail;
	}
	if (rc < 0) {
		LOG_ERR("Failed to get persistent buffer partition sectors: %d", rc);
		goto fail;
	}
	if (sector_cnt < 2) {
		LOG_ERR("Persistent buffer partition needs at least 2 sectors, has %u", sector_cnt);
		rc = -EINVAL;
		goto fail;
	}
	pb.sector_cnt = sector_cnt;

	rc = check_write_block_size();
	if (rc < 0) {
		goto fail;
	}

	rc = init_fcb();
	if (rc == -ENOMSG || (rc == 0 && !format_matches())) {
		LOG_WRN("Persistent buffer holds data in an unknown format, erasing it");
		rc = erase_partition();
		if (rc == 0) {
			rc = init_fcb();
		}
	}
	if (rc < 0) {
		LOG_ERR("Failed to initialize persistent buffer: %d", rc);
		goto fail;
	}

	size_t min_sector_size = SIZE_MAX;

	pb.total_size = 0;
	for (uint32_t i = 0; i < pb.sector_cnt; i++) {
		pb.total_size += pb.sectors[i].fs_size;
		min_sector_size = MIN(min_sector_size, pb.sectors[i].fs_size);
	}

	pb.first_elem_off = align_up(PB_FCB_SECTOR_HEADER_LEN);
	/* An entry's length field takes 2 bytes once the entry is 128 bytes or longer. */
	pb.max_entry_len = MIN(FCB_MAX_LEN, min_sector_size - pb.first_elem_off - align_up(2) -
						    align_up(PB_FCB_CRC_LEN));
	pb.max_entry_len = ROUND_DOWN(pb.max_entry_len, pb.fcb.f_align);

	pb.session_written = false;
	pb.carry_valid = false;
	reset_reader();
	pb.last_sent_run = spotflow_session_metadata_get_run_id();

	atomic_set(&pb.state, SPOTFLOW_PB_STATE_READY);
	k_mutex_unlock(&pb_lock);

	LOG_INF("Persistent buffer ready: %u sectors, %zu bytes", pb.sector_cnt, pb.total_size);
	return 0;

fail:
	atomic_set(&pb.state, SPOTFLOW_PB_STATE_FAILED);
	k_mutex_unlock(&pb_lock);
	return rc;
}

enum spotflow_pb_state spotflow_pb_get_state(void)
{
	return (enum spotflow_pb_state)atomic_get(&pb.state);
}

int spotflow_pb_persist(void)
{
	int rc = 0;
	int stored = 0;

	if (spotflow_pb_get_state() != SPOTFLOW_PB_STATE_READY) {
		return spotflow_pb_get_state() == SPOTFLOW_PB_STATE_PENDING ? -EAGAIN : -ENODEV;
	}

	k_mutex_lock(&pb_lock, K_FOREVER);

	rc = encode_session_record();
	if (rc == 0) {
		/* Leave room for a session record in every entry. */
		size_t cap = pb.max_entry_len - SPOTFLOW_PB_MSG_HEADER_LEN - pb.session_record_len;

		while (true) {
			size_t data_len = 0;
			size_t n = collect_batch(cap, &data_len);

			if (n == 0) {
				break;
			}

			rc = write_batch(n, data_len);
			for (size_t i = 0; i < n; i++) {
				release_msg(&pb.batch[i]);
			}
			if (rc < 0) {
				LOG_ERR("Failed to write %zu messages to flash: %d", n, rc);
				add_dropped(n);
				break;
			}
			stored += n;
		}
	}

	k_mutex_unlock(&pb_lock);

	report_dropped();

	return rc < 0 ? rc : stored;
}

int spotflow_pb_send_next(void)
{
	int rc;
	uint16_t len = 0;
	uint8_t* payload = NULL;

	if (spotflow_pb_get_state() != SPOTFLOW_PB_STATE_READY) {
		return 0;
	}

	if (!spotflow_transport_is_ready()) {
		return -EAGAIN;
	}

	if (atomic_cas(&pb.session_restarted, 1, 0)) {
		/* The gateway or broker has just received the current session metadata. */
		pb.last_sent_run = spotflow_session_metadata_get_run_id();
	}

	k_mutex_lock(&pb_lock, K_FOREVER);

	rc = next_message(&len);
	if (rc == -ENOENT) {
		k_mutex_unlock(&pb_lock);
		return 0;
	}
	if (rc == 0) {
		payload = k_malloc(len);
		if (payload == NULL) {
			LOG_WRN("Not enough heap to send a %u-byte buffered message", len);
			rc = -EAGAIN;
		}
	}
	if (rc == 0) {
		rc = fcb_flash_read(&pb.fcb, pb.rd.loc.fe_sector,
				    pb.rd.loc.fe_data_off + pb.rd.msg_off + SPOTFLOW_PB_MSG_HEADER_LEN,
				    payload, len);
	}
	if (rc < 0) {
		k_mutex_unlock(&pb_lock);
		k_free(payload);
		return rc;
	}

	uint32_t epoch = pb.rd.epoch;
	bool run_known = pb.rd.run_known;
	uint64_t run_id = pb.rd.run_id;
	uint16_t session_len = pb.rd.session_len;

	k_mutex_unlock(&pb_lock);

	/* Messages are sent without holding the lock, so that sending does not block persisting. */
	if (run_known && run_id != pb.last_sent_run) {
		if (run_id == spotflow_session_metadata_get_run_id()) {
			/* Back at the current boot: send the full current session metadata. */
			rc = spotflow_session_metadata_send();
		} else {
			rc = spotflow_transport_send_ingest_cbor(
				pb.rd_session + SPOTFLOW_PB_RUN_ID_LEN,
				session_len - SPOTFLOW_PB_RUN_ID_LEN);
		}
		if (rc < 0) {
			k_free(payload);
			return rc;
		}
		pb.last_sent_run = run_id;
	}

	rc = spotflow_transport_send_ingest_cbor(payload, len);
	k_free(payload);
	if (rc < 0) {
		/* The message stays in the ring and is sent again. */
		return rc;
	}

	k_mutex_lock(&pb_lock, K_FOREVER);
	if (pb.rd.epoch == epoch) {
		pb.rd.msg_off += SPOTFLOW_PB_MSG_HEADER_LEN + len;
	}
	k_mutex_unlock(&pb_lock);

	return 1;
}

void spotflow_pb_on_session_start(void)
{
	atomic_set(&pb.session_restarted, 1);
}

void spotflow_pb_on_enqueued(struct k_msgq* msgq)
{
	if (k_msgq_num_used_get(msgq) * 2 >= msgq->max_msgs) {
		spotflow_pb_wake_persister();
	}
}

int spotflow_persistent_buffer_flush(void)
{
	int rc = spotflow_pb_persist();

	return rc < 0 ? rc : 0;
}

int spotflow_persistent_buffer_get_stats(struct spotflow_persistent_buffer_stats* stats)
{
	if (stats == NULL) {
		return -EINVAL;
	}
	if (spotflow_pb_get_state() != SPOTFLOW_PB_STATE_READY) {
		return spotflow_pb_get_state() == SPOTFLOW_PB_STATE_PENDING ? -EAGAIN : -ENODEV;
	}

	k_mutex_lock(&pb_lock, K_FOREVER);

	size_t used = 0;
	struct flash_sector* sector = pb.fcb.f_oldest;

	while (sector != pb.fcb.f_active.fe_sector) {
		used += sector->fs_size;
		sector = next_sector(sector);
	}
	used += pb.fcb.f_active.fe_elem_off;

	uint32_t stored = 0;
	struct fcb_entry loc = pb.rd.loc;

	if (pb.rd.in_entry) {
		stored += count_messages(&loc, pb.rd.msg_off);
	}
	while (fcb_getnext(&pb.fcb, &loc) == 0) {
		stored += count_messages(&loc, 0);
	}

	stats->bytes_used = used;
	stats->bytes_free = pb.total_size - used;
	stats->messages_stored = stored;
	stats->messages_dropped = pb.dropped_total;

	k_mutex_unlock(&pb_lock);

	return 0;
}

int spotflow_persistent_buffer_clear(void)
{
	if (spotflow_pb_get_state() != SPOTFLOW_PB_STATE_READY) {
		return spotflow_pb_get_state() == SPOTFLOW_PB_STATE_PENDING ? -EAGAIN : -ENODEV;
	}

	k_mutex_lock(&pb_lock, K_FOREVER);

	int rc = fcb_clear(&pb.fcb);

	reset_reader();
	pb.session_written = false;

	k_mutex_unlock(&pb_lock);

	if (rc < 0) {
		LOG_ERR("Failed to clear persistent buffer: %d", rc);
	}

	return rc;
}

/* FCB writes its 8-byte sector header in one write, so it only works on flash whose write block
 * is at most 8 bytes. The nRF54L RRAM driver uses 16-byte blocks by default. */
static int check_write_block_size(void)
{
	const struct flash_area* fa;
	int rc = flash_area_open(PB_PARTITION_ID, &fa);

	if (rc < 0) {
		LOG_ERR("Failed to open persistent buffer partition: %d", rc);
		return rc;
	}

	uint32_t align = flash_area_align(fa);

	flash_area_close(fa);

	if (align > PB_MAX_WRITE_ALIGN) {
		LOG_ERR("Flash write block size %u is not supported by FCB (max %d)", align,
			PB_MAX_WRITE_ALIGN);
		return -ENOTSUP;
	}

	return 0;
}

static int init_fcb(void)
{
	memset(&pb.fcb, 0, sizeof(pb.fcb));
	pb.fcb.f_magic = PB_MAGIC;
	pb.fcb.f_version = SPOTFLOW_PB_FORMAT_VERSION;
	pb.fcb.f_sectors = pb.sectors;
	pb.fcb.f_sector_cnt = pb.sector_cnt;
	pb.fcb.f_scratch_cnt = 0;

	return fcb_init(PB_PARTITION_ID, &pb.fcb);
}

/* FCB stores f_version in each sector header but does not check it on init. */
static bool format_matches(void)
{
	for (uint32_t i = 0; i < pb.sector_cnt; i++) {
		uint8_t header[PB_FCB_SECTOR_HEADER_LEN];

		if (flash_area_read(pb.fcb.fap, pb.sectors[i].fs_off, header, sizeof(header)) != 0) {
			return false;
		}

		bool erased = true;

		for (size_t j = 0; j < 4; j++) {
			erased = erased && header[j] == pb.fcb.f_erase_value;
		}

		/* Layout of struct fcb_disk_area: 4-byte magic, 1-byte version, pad, 2-byte ID. */
		if (!erased && header[4] != SPOTFLOW_PB_FORMAT_VERSION) {
			return false;
		}
	}

	return true;
}

static int erase_partition(void)
{
	const struct flash_area* fa;
	int rc = flash_area_open(PB_PARTITION_ID, &fa);

	if (rc < 0) {
		return rc;
	}

	rc = flash_area_flatten(fa, 0, fa->fa_size);
	flash_area_close(fa);

	return rc;
}

static void reset_reader(void)
{
	uint32_t epoch = pb.rd.epoch;

	memset(&pb.rd, 0, sizeof(pb.rd));
	pb.rd.epoch = epoch + 1;
}

static int encode_session_record(void)
{
	size_t cbor_len = 0;
	int rc = spotflow_session_metadata_encode_for_replay(
		pb.session_record + SPOTFLOW_PB_RUN_ID_LEN,
		sizeof(pb.session_record) - SPOTFLOW_PB_RUN_ID_LEN, &cbor_len);

	if (rc < 0) {
		LOG_ERR("Failed to encode session record: %d", rc);
		return rc;
	}

	sys_put_le64(spotflow_session_metadata_get_run_id(), pb.session_record);
	pb.session_record_len = SPOTFLOW_PB_RUN_ID_LEN + cbor_len;

	return 0;
}

/* Takes queued RAM messages, in the order heartbeat, metrics, logs, while they fit into cap
 * bytes of entry data. A message that does not fit is kept for the next batch. */
static size_t collect_batch(size_t cap, size_t* data_len)
{
	size_t n = 0;
	size_t total = 0;
	struct pb_msg msg;
	bool heartbeat_taken = false;

	if (pb.carry_valid) {
		pb.carry_valid = false;
		msg = pb.carry;
		heartbeat_taken = msg.source == PB_SOURCE_STATIC;
		if (!add_to_batch(&msg, &n, &total, cap)) {
			goto done;
		}
	}

#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
	size_t heartbeat_len = 0;

	/* The heartbeat buffer holds one message; do not overwrite one that is in the batch. */
	if (!heartbeat_taken &&
	    spotflow_metrics_heartbeat_take(pb.heartbeat, sizeof(pb.heartbeat), &heartbeat_len)) {
		msg = (struct pb_msg){
			.kind = SPOTFLOW_PB_KIND_METRIC,
			.source = PB_SOURCE_STATIC,
			.len = heartbeat_len,
			.payload = pb.heartbeat,
		};
		if (!add_to_batch(&msg, &n, &total, cap)) {
			goto done;
		}
	}
#else
	ARG_UNUSED(heartbeat_taken);
#endif /* CONFIG_SPOTFLOW_METRICS_HEARTBEAT */

#ifdef CONFIG_SPOTFLOW_METRICS
	struct spotflow_metric_msg* metric;

	while (n < PB_BATCH_MAX_MSGS && k_msgq_get(&g_spotflow_metrics_msgq, &metric, K_NO_WAIT) == 0) {
		msg = (struct pb_msg){
			.kind = SPOTFLOW_PB_KIND_METRIC,
			.source = PB_SOURCE_METRIC,
			.len = MIN(metric->len, UINT16_MAX),
			.payload = metric->payload,
			.owner = metric,
		};
		if (!add_to_batch(&msg, &n, &total, cap)) {
			goto done;
		}
	}
#endif /* CONFIG_SPOTFLOW_METRICS */

#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
	struct spotflow_log_msg* log;

	while (n < PB_BATCH_MAX_MSGS && k_msgq_get(&g_spotflow_logs_msgq, &log, K_NO_WAIT) == 0) {
		msg = (struct pb_msg){
			.kind = SPOTFLOW_PB_KIND_LOG,
			.source = PB_SOURCE_LOG,
			.len = MIN(log->len, UINT16_MAX),
			.payload = log->payload,
			.owner = log,
		};
		if (!add_to_batch(&msg, &n, &total, cap)) {
			goto done;
		}
	}
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND */

done:
	*data_len = total;
	return n;
}

/* Returns false if the message does not fit and was kept as the carry for the next batch. */
static bool add_to_batch(struct pb_msg* msg, size_t* n, size_t* total, size_t cap)
{
	size_t size = SPOTFLOW_PB_MSG_HEADER_LEN + msg->len;

	if (size > cap) {
		LOG_WRN("Message of %u bytes does not fit into a flash sector, dropping it",
			msg->len);
		release_msg(msg);
		add_dropped(1);
		return true;
	}

	if (*total + size > cap || *n >= PB_BATCH_MAX_MSGS) {
		pb.carry = *msg;
		pb.carry_valid = true;
		return false;
	}

	pb.batch[(*n)++] = *msg;
	*total += size;
	return true;
}

static void release_msg(struct pb_msg* msg)
{
	switch (msg->source) {
#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
	case PB_SOURCE_LOG: {
		struct spotflow_log_msg* log = msg->owner;

		k_free(log->payload);
		k_free(log);
		break;
	}
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND */
#ifdef CONFIG_SPOTFLOW_METRICS
	case PB_SOURCE_METRIC: {
		struct spotflow_metric_msg* metric = msg->owner;

		k_free(metric->payload);
		k_free(metric);
		break;
	}
#endif /* CONFIG_SPOTFLOW_METRICS */
	default:
		break;
	}
}

static int write_batch(size_t n, size_t data_len)
{
	int rc;
	struct fcb_entry loc;
	bool with_session = needs_session_record(data_len);

	if (with_session) {
		data_len += SPOTFLOW_PB_MSG_HEADER_LEN + pb.session_record_len;
	}

	/* fcb_append() fails with -ENOSPC when the ring is full; drop the oldest sector and retry.
	 * That only happens when a new sector is needed, so the entry has a session record. */
	for (uint32_t attempt = 0;; attempt++) {
		rc = fcb_append(&pb.fcb, data_len, &loc);
		if (rc != -ENOSPC || attempt >= pb.sector_cnt) {
			break;
		}

		rc = drop_oldest_sector();
		if (rc < 0) {
			return rc;
		}
	}
	if (rc < 0) {
		return rc;
	}

	if (!with_session && loc.fe_elem_off == pb.first_elem_off) {
		LOG_WRN("Entry started a sector without a session record");
	}

	struct pb_writer writer;
	uint8_t header[SPOTFLOW_PB_MSG_HEADER_LEN];

	writer_init(&writer, &loc);

	if (with_session) {
		header[0] = SPOTFLOW_PB_KIND_SESSION;
		sys_put_le16(pb.session_record_len, &header[1]);
		writer_put(&writer, header, sizeof(header));
		writer_put(&writer, pb.session_record, pb.session_record_len);
	}

	for (size_t i = 0; i < n; i++) {
		header[0] = pb.batch[i].kind;
		sys_put_le16(pb.batch[i].len, &header[1]);
		writer_put(&writer, header, sizeof(header));
		writer_put(&writer, pb.batch[i].payload, pb.batch[i].len);
	}

	writer_finish(&writer);
	if (writer.rc < 0) {
		return writer.rc;
	}

	rc = fcb_append_finish(&pb.fcb, &loc);
	if (rc < 0) {
		return rc;
	}

	if (with_session) {
		pb.session_written = true;
	}

	return 0;
}

/* Whether the next entry must start with a session record: on the boot's first write and
 * whenever the entry starts a new sector. Mirrors the space check of fcb_append(). */
static bool needs_session_record(size_t data_len)
{
	const struct fcb_entry* active = &pb.fcb.f_active;

	if (!pb.session_written || active->fe_elem_off <= pb.first_elem_off) {
		return true;
	}

	return active->fe_elem_off + entry_flash_size(data_len) > active->fe_sector->fs_size;
}

/* Flash space of an FCB entry: length field (1 or 2 bytes), data and CRC, each aligned. */
static size_t entry_flash_size(size_t data_len)
{
	return align_up(data_len < 0x80 ? 1 : 2) + align_up(data_len) + align_up(PB_FCB_CRC_LEN);
}

static uint32_t align_up(uint32_t len)
{
	uint8_t align = MAX(pb.fcb.f_align, 1);

	return ROUND_UP(len, align);
}

static void writer_init(struct pb_writer* w, const struct fcb_entry* loc)
{
	w->sector = loc->fe_sector;
	w->off = loc->fe_data_off;
	w->align = MAX(pb.fcb.f_align, 1);
	w->carry_len = 0;
	w->rc = 0;
}

/* Writes straight from the source buffer; only bytes that do not fill a whole write block go
 * through the carry buffer. */
static void writer_put(struct pb_writer* w, const uint8_t* data, size_t len)
{
	if (w->rc < 0) {
		return;
	}

	if (w->carry_len > 0) {
		size_t fill = MIN(len, (size_t)(w->align - w->carry_len));

		memcpy(&w->carry[w->carry_len], data, fill);
		w->carry_len += fill;
		data += fill;
		len -= fill;

		if (w->carry_len < w->align) {
			return;
		}

		w->rc = fcb_flash_write(&pb.fcb, w->sector, w->off, w->carry, w->align);
		w->off += w->align;
		w->carry_len = 0;
		if (w->rc < 0) {
			return;
		}
	}

	size_t direct = len - (len % w->align);

	if (direct > 0) {
		w->rc = fcb_flash_write(&pb.fcb, w->sector, w->off, data, direct);
		w->off += direct;
		data += direct;
		len -= direct;
		if (w->rc < 0) {
			return;
		}
	}

	memcpy(w->carry, data, len);
	w->carry_len = len;
}

static void writer_finish(struct pb_writer* w)
{
	if (w->rc < 0 || w->carry_len == 0) {
		return;
	}

	memset(&w->carry[w->carry_len], pb.fcb.f_erase_value, w->align - w->carry_len);
	w->rc = fcb_flash_write(&pb.fcb, w->sector, w->off, w->carry, w->align);
	w->carry_len = 0;
}

/* Erases the oldest sector to make room. Counts the unsent messages it held as dropped. */
static int drop_oldest_sector(void)
{
	struct flash_sector* oldest = pb.fcb.f_oldest;
	bool reader_in_sector = pb.rd.loc.fe_sector == NULL || pb.rd.loc.fe_sector == oldest;
	uint32_t lost = count_unsent_in_sector(oldest);

	int rc = fcb_rotate(&pb.fcb);

	if (rc < 0) {
		LOG_ERR("Failed to erase the oldest persistent buffer sector: %d", rc);
		return rc;
	}

	if (reader_in_sector) {
		reset_reader();
	}

	if (lost > 0) {
		add_dropped(lost);
		LOG_WRN("Persistent buffer full, dropped %u oldest messages", lost);
	}

	return 0;
}

/* Erases sectors the sender has moved past. They are never the active sector. */
static void release_sent_sectors(const struct flash_sector* current)
{
	while (pb.fcb.f_oldest != current && pb.fcb.f_oldest != pb.fcb.f_active.fe_sector) {
		int rc = fcb_rotate(&pb.fcb);

		if (rc < 0) {
			LOG_ERR("Failed to erase a sent persistent buffer sector: %d", rc);
			return;
		}
	}
}

/* Positions the reader at the next log or metric. Session records on the way update the run
 * of the following messages. Returns -ENOENT when the ring holds no more messages. */
static int next_message(uint16_t* len)
{
	while (true) {
		if (!pb.rd.in_entry) {
			struct fcb_entry loc = pb.rd.loc;
			int rc = fcb_getnext(&pb.fcb, &loc);

			if (rc == -ENOTSUP || rc == -ENOENT) {
				return -ENOENT;
			}
			if (rc < 0) {
				return rc;
			}

			release_sent_sectors(loc.fe_sector);
			pb.rd.loc = loc;
			pb.rd.in_entry = true;
			pb.rd.msg_off = 0;
		}

		uint8_t kind;
		uint16_t msg_len;
		int rc = read_msg_header(&pb.rd.loc, pb.rd.msg_off, &kind, &msg_len);

		if (rc == -ENOENT) {
			pb.rd.in_entry = false;
			continue;
		}
		if (rc < 0) {
			return rc;
		}

		if (kind != SPOTFLOW_PB_KIND_SESSION) {
			*len = msg_len;
			return 0;
		}

		pb.rd.run_known = false;
		if (msg_len > SPOTFLOW_PB_RUN_ID_LEN && msg_len <= sizeof(pb.rd_session)) {
			rc = fcb_flash_read(&pb.fcb, pb.rd.loc.fe_sector,
					    pb.rd.loc.fe_data_off + pb.rd.msg_off +
						    SPOTFLOW_PB_MSG_HEADER_LEN,
					    pb.rd_session, msg_len);
			if (rc < 0) {
				return rc;
			}
			pb.rd.run_known = true;
			pb.rd.run_id = sys_get_le64(pb.rd_session);
			pb.rd.session_len = msg_len;
		}
		pb.rd.msg_off += SPOTFLOW_PB_MSG_HEADER_LEN + msg_len;
	}
}

/* Returns -ENOENT at the end of the entry or when the rest of the entry is malformed. */
static int read_msg_header(const struct fcb_entry* loc, uint16_t off, uint8_t* kind,
			   uint16_t* len)
{
	uint8_t header[SPOTFLOW_PB_MSG_HEADER_LEN];

	if (off + SPOTFLOW_PB_MSG_HEADER_LEN > loc->fe_data_len) {
		return -ENOENT;
	}

	int rc = fcb_flash_read(&pb.fcb, loc->fe_sector, loc->fe_data_off + off, header,
				sizeof(header));
	if (rc < 0) {
		return rc;
	}

	*kind = header[0];
	*len = sys_get_le16(&header[1]);

	if (*kind < SPOTFLOW_PB_KIND_LOG || *kind > SPOTFLOW_PB_KIND_SESSION ||
	    off + SPOTFLOW_PB_MSG_HEADER_LEN + *len > loc->fe_data_len) {
		LOG_WRN("Skipping malformed persistent buffer entry");
		return -ENOENT;
	}

	return 0;
}

/* Counts logs and metrics in an entry, starting at the given data offset. */
static uint32_t count_messages(const struct fcb_entry* loc, uint16_t from_off)
{
	uint32_t count = 0;
	uint16_t off = from_off;
	uint8_t kind;
	uint16_t len;

	while (read_msg_header(loc, off, &kind, &len) == 0) {
		if (kind != SPOTFLOW_PB_KIND_SESSION) {
			count++;
		}
		off += SPOTFLOW_PB_MSG_HEADER_LEN + len;
	}

	return count;
}

static uint32_t count_unsent_in_sector(const struct flash_sector* sector)
{
	uint32_t count = 0;
	bool reader_here = pb.rd.loc.fe_sector == sector;
	struct fcb_entry loc = { .fe_sector = (struct flash_sector*)sector };

	while (fcb_getnext(&pb.fcb, &loc) == 0 && loc.fe_sector == sector) {
		if (reader_here && loc.fe_elem_off < pb.rd.loc.fe_elem_off) {
			continue;
		}
		if (reader_here && loc.fe_elem_off == pb.rd.loc.fe_elem_off) {
			if (pb.rd.in_entry) {
				count += count_messages(&loc, pb.rd.msg_off);
			}
			continue;
		}
		count += count_messages(&loc, 0);
	}

	return count;
}

static struct flash_sector* next_sector(struct flash_sector* sector)
{
	sector++;
	if (sector >= &pb.sectors[pb.sector_cnt]) {
		sector = &pb.sectors[0];
	}
	return sector;
}

static void add_dropped(uint32_t count)
{
	pb.dropped_total += count;
	atomic_add(&pb.dropped_unreported, count);
}

/* Reports dropped messages as a standard metric, which is buffered like any other. */
static void report_dropped(void)
{
#ifdef CONFIG_SPOTFLOW_METRICS
	static struct spotflow_metric_int* dropped_metric;

	atomic_val_t dropped = atomic_get(&pb.dropped_unreported);

	if (dropped == 0) {
		return;
	}

	if (dropped_metric == NULL) {
		int rc = spotflow_register_metric_int(PB_DROPPED_METRIC_NAME,
						      SPOTFLOW_AGG_INTERVAL_NONE, &dropped_metric);
		if (rc < 0) {
			LOG_DBG("Failed to register dropped messages metric: %d", rc);
			return;
		}
	}

	if (spotflow_report_metric_int(dropped_metric, dropped) == 0) {
		atomic_sub(&pb.dropped_unreported, dropped);
	}
#endif /* CONFIG_SPOTFLOW_METRICS */
}
