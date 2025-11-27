#include "esp_system.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_core_dump.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "logging/spotflow_log_backend.h"
#include "coredump/spotflow_coredump.h"
#include "coredump/spotflow_coredump_cbor.h"
#include "coredump/spotflow_coredump_queue.h"

#ifdef CONFIG_SPOTFLOW_USE_BUILD_ID
	#include "buildid/spotflow_build_id.h"
#endif

// static const char* TAG = "SPOTFLOW_COREDUMP";

#define COREDUMP_PARTITION_NAME "coredump"

typedef struct {
	size_t size;
	size_t offset;
	int chunk_ordinal;
	uint32_t coredump_id;
} coredump_info_t;

static coredump_info_t coredump_info = { 0 };

/**
 * @brief If coredump Partion is available return true otherwise false
 * 
 * @return true 
 * @return false 
 */
bool spotflow_is_coredump_available(void)
{
	const esp_partition_t* part = esp_partition_find_first(
	    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);

	if (part == NULL) {
		SPOTFLOW_LOG( "No coredump partition found.");
		return false;
	}

	// Get the actual size of the coredump partition
	size_t partition_size = part->size;
	if (partition_size == 0) {
		SPOTFLOW_LOG( "Coredump partition size is zero.");
		return false;
	}
	size_t coredump_addr = 0;
	esp_err_t err = esp_core_dump_image_get(&coredump_addr, &partition_size);
	
	if (err != ESP_OK) {
		SPOTFLOW_LOG( "No Coredump found.");
		return false;
	}
	return true;
}

/**
 * @brief 
 * 
 * @return esp_err_t 
 */
esp_err_t spotflow_coredump_backend(void)
{
	size_t coredump_addr = 0;
	size_t coredump_size = 0;

	// Retrieve the coredump image address and size
	esp_err_t err = esp_core_dump_image_get(&coredump_addr, &coredump_size);
	if (err != ESP_OK) {
		SPOTFLOW_LOG( "No Coredump found.");
		return err;
	}

	// If the coredump size is 0, Coredump is empty
	if (coredump_size == 0) {
		SPOTFLOW_LOG( "Coredump is empty.");
		return ESP_ERR_INVALID_STATE;
	}

	// Log the coredump information
	SPOTFLOW_LOG( "Coredump address: 0x%08X, size: 0x%08X bytes", (unsigned int)coredump_addr,
		 (int)coredump_size);

	// Find the coredump partition
	const esp_partition_t* part = esp_partition_find_first(
	    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
	if (!part) {
		SPOTFLOW_LOG( "Coredump partition not found.");
		return ESP_ERR_NOT_FOUND;
	}

	// Adjust address to partition offset
	coredump_addr = coredump_addr - part->address;
	SPOTFLOW_LOG( "Adjusted coredump address: 0x%08X, partition address: 0x%08X",
		 (unsigned int)coredump_addr, (unsigned int)part->address);

	// Initialize coredump info
	coredump_info.size = coredump_size;
	coredump_info.offset = 0;
	coredump_info.chunk_ordinal = 0;
	coredump_info.coredump_id = esp_random(); // Generate random coredump ID

	SPOTFLOW_LOG( "Starting coredump processing with ID: 0x%08X, size: %zu",
		 (unsigned int)coredump_info.coredump_id, coredump_info.size);

	// Allocate buffer for one chunk only
	size_t chunk_size = CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE;
	uint8_t* chunk_buffer = malloc(chunk_size);

	if (!chunk_buffer) {
		SPOTFLOW_LOG( "Failed to allocate memory for chunk buffer.");
		return ESP_ERR_NO_MEM;
	}

	// Process coredump in chunks
	while (coredump_info.offset < coredump_info.size) {
		// Calculate remaining size and current chunk size
		size_t remaining_size = coredump_info.size - coredump_info.offset;
		size_t current_chunk_size =
		    (remaining_size < chunk_size) ? remaining_size : chunk_size;

		// Read the chunk from flash
		err = esp_partition_read(part, coredump_addr + coredump_info.offset, chunk_buffer,
					 current_chunk_size);
		if (err != ESP_OK) {
			free(chunk_buffer);
			SPOTFLOW_LOG( "Failed to read coredump chunk at offset %zu.",
				 coredump_info.offset);
			return err;
		}

		// Check if this is the last chunk
		bool is_last_chunk =
		    (coredump_info.offset + current_chunk_size) >= coredump_info.size;
		if (is_last_chunk) {
			SPOTFLOW_LOG( "Processing last chunk of coredump\n");
		}

		// Get build ID (only for first chunk)
		const uint8_t* build_id = NULL;
		uint16_t build_id_len = 0;

#ifdef CONFIG_SPOTFLOW_USE_BUILD_ID
		if (coredump_info.chunk_ordinal == 0) {
			int rc = spotflow_build_id_get(&build_id, &build_id_len);
			if (rc != 0) {
				SPOTFLOW_LOG( "Failed to get build ID for coredump: %d", rc);
			} else {
				SPOTFLOW_LOG( "Build ID retrieved, length: %zu", build_id_len);
			}
		}
#endif

		// Encode coredump chunk to CBOR
		uint8_t* cbor_data = NULL;
		size_t cbor_data_len = 0;
		int rc = spotflow_cbor_encode_coredump(
		    chunk_buffer, current_chunk_size, coredump_info.chunk_ordinal,
		    coredump_info.coredump_id, is_last_chunk, build_id, build_id_len, &cbor_data,
		    &cbor_data_len);

		if (rc < 0) {
			free(chunk_buffer);
			SPOTFLOW_LOG( "Failed to encode coredump chunk: %d", rc);
			return ESP_FAIL;
		}

		// keep retrying to push data until the mqtt function is able to read and clear it
		do {
			rc = spotflow_queue_coredump_push(cbor_data, cbor_data_len);
			vTaskDelay(pdMS_TO_TICKS(50)); //50 ticks wait
		} while(rc < 0);

		// Free the CBOR data after sending
		free(cbor_data);

		// Update progress
		coredump_info.chunk_ordinal++;
		coredump_info.offset += current_chunk_size;

		SPOTFLOW_LOG( "Sent chunk %d: %zu/%zu bytes (%.1f%%)\n",
			 coredump_info.chunk_ordinal - 1, coredump_info.offset, coredump_info.size,
			 (float)coredump_info.offset * 100.0f / coredump_info.size);
	}

	free(chunk_buffer);
	SPOTFLOW_LOG( "Successfully processed and sent all %zu bytes of coredump data in %d chunks\n",
		 coredump_info.size, coredump_info.chunk_ordinal);
	
	spotflow_coredump_cleanup(); // call the cleanup as the data is in the mqtt court. 
	return ESP_OK;
}

/**
 * @brief Erase the coredump image.
 * 
 */
void spotflow_coredump_cleanup()
{
	esp_core_dump_image_erase(); //Erase the image after upload
}