#include "spotflow_transport_uart.h"
#include "main.h"

extern UART_HandleTypeDef huart1;

static bool spotflow_uart_write(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return true;
    }

    HAL_StatusTypeDef rc = HAL_UART_Transmit(&huart1, (uint8_t *)data, len, SPOTFLOW_UART_TIMEOUT_MS);
    return (rc == HAL_OK);
}

bool spotflow_transport_send_frame(const SpotflowFrame *frame)
{
    if (frame == NULL) {
        return false;
    }

#if (SPOTFLOW_TRANSPORT_BACKEND == SPOTFLOW_TRANSPORT_UART)
    uint8_t header[2];
    header[0] = (uint8_t)(frame->len & 0xFFU);
    header[1] = (uint8_t)((frame->len >> 8) & 0xFFU);

    if (!spotflow_uart_write(header, sizeof(header))) {
        return false;
    }

    if (!spotflow_uart_write(frame->payload, frame->len)) {
        return false;
    }

    return true;
#else
    (void)frame;
    return false;
#endif
}
