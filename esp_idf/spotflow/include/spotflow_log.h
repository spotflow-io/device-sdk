#ifndef SPOTFLOW_LOG_H
#define SPOTFLOW_LOG_H

#if CONFIG_SPOTFLOW_DEBUG_MESSAGE_TERMINAL
    #define SPOTFLOW_LOG(fmt, ...) printf("[SPOTFLOW] " fmt, ##__VA_ARGS__)
#else
    #define SPOTFLOW_LOG(fmt, ...) do {} while(0)
#endif

#endif