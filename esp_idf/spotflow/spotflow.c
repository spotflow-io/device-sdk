#include <stdio.h>
#include "spotflow.h"

#ifndef CONFIG_USE_JSON_PAYLOAD
    #include "cbor.h"
#endif


vprintf_like_t original_vprintf = NULL;

// Initilize the spotlow log backend
/**
 * @brief 
 * @details To utilize the esp_log_set_vprintf function to expose the logs
 */
void spotflow_init(void)
{
    original_vprintf = esp_log_set_vprintf(spotflow_log_backend);
}