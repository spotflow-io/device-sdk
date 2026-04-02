#include "spotflow.h"
#include "logging/spotflow_log_backend.h"
#include "queue/spotflow_queue.h"
#include "network/spotflow_network.h"

/**
 * @brief Spotflow init function. Initializes logging, queue, and network.
 *
 */
void spotflow_init(void)
{
    spotflow_log_init();
    spotflow_queue_init();
    spotflow_network_init();
    SPOTFLOW_LOGI("SPOTFLOW", "Spotflow initialized");
}
