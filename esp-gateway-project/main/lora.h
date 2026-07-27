#ifndef LORA_H
#define LORA_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/spi.h"

// Hardware Pin Definitions for Ra-01SH
#define LORA_RST_GPIO   GPIO_NUM_4
#define LORA_NSS_GPIO   GPIO_NUM_5
#define LORA_BUSY_GPIO  GPIO_NUM_16
#define LORA_DIO1_GPIO  GPIO_NUM_15

// SWSD003 header forward declarations for data types
#include "sx126x.h"

/**
 * @brief Configuration struct for LoRa parameters
 */
typedef struct {
    uint32_t frequency_hz;            // RF frequency in Hz (e.g., 915000000)
    int8_t power_dbm;                 // Output Power in dBm (-9 to +22)
    sx126x_lora_sf_t sf;   // Spreading Factor (e.g., SX126X_LORA_SF12)
    sx126x_lora_bw_t bw;   // Bandwidth (e.g., SX126X_LORA_BW_125)
    sx126x_lora_cr_t cr;   // Coding Rate (e.g., SX126X_LORA_CR_4_5)
    uint16_t preamble_length;         // Preamble length in symbols
    bool header_explicit;             // true = EXPLICIT, false = IMPLICIT
    uint8_t payload_length;           // Maximum / fixed payload length
    bool crc_on;                      // true = CRC enabled, false = disabled
    bool invert_iq;                   // true = Standard Rx/Tx Inverted IQ, false = Standard IQ
} lora_config_t;

/**
 * @brief Helper Macro providing default initialization values (915 MHz, SF12, BW125, CR4/5)
 */
#define LORA_CONFIG_DEFAULT() {                             \
    .frequency_hz    = 915000000,                           \
    .power_dbm       = 14,                                  \
    .sf              = SX126X_LORA_SF12,                    \
    .bw              = SX126X_LORA_BW_125,                  \
    .cr              = SX126X_LORA_CR_4_5,                  \
    .preamble_length = 8,                                   \
    .header_explicit = true,                                \
    .payload_length  = 255,                                 \
    .crc_on          = true,                                \
    .invert_iq       = false                                \
}

// Function Declarations
bool lora_init(const lora_config_t *config);
bool lora_check_connection(void);

/**
 * @brief Transmit a packet over LoRa (blocking until timeout or completion)
 * 
 * @param data Pointer to byte buffer to send
 * @param length Length of data payload
 * @param timeout_ms Timeout in milliseconds to wait for TX completion
 * @return true Transmit successful
 * @return false Transmit failed or timed out
 */
bool lora_send_packet(const uint8_t *data, uint8_t length, uint32_t timeout_ms);

/**
 * @brief Receive a LoRa packet (blocking wait)
 * 
 * @param buffer Buffer to fill with received data
 * @param max_length Maximum bytes the destination buffer can accept
 * @param rx_length Pointer to store actual bytes received
 * @param timeout_ms Max time to wait in ms (0 = non-blocking continuous listen check)
 * @return true Packet received successfully
 * @return false Timeout or error
 */
bool lora_receive_packet(uint8_t *buffer, uint8_t max_length, uint8_t *rx_length, uint32_t timeout_ms);

#endif // LORA_H
