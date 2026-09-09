#ifndef LORA_H
#define LORA_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi.h"
#include "lora_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Function Declarations
bool lora_init(const lora_config_t *config);
bool lora_check_connection(void);
bool lora_send_packet(const uint8_t *data, uint8_t length, uint32_t timeout_ms);
bool lora_receive_packet(uint8_t *buffer, uint8_t max_length, uint8_t *rx_length, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // LORA_H
