#ifndef SPOTFLOW_MINIMAL_COREDUMP_H
#define SPOTFLOW_MINIMAL_COREDUMP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_minimal_coredump_process(uint8_t* workspace, size_t workspace_size);
void spotflow_coredump_sent(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_MINIMAL_COREDUMP_H */
