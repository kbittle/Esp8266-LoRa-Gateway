#ifndef LORA_DEFS_H
#define LORA_DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Hardware Pin Definitions for Ra-01H (SX1276/77/78-family module)
// -----------------------------------------------------------------------------
#define LORA_RST_GPIO   GPIO_NUM_4
#define LORA_NSS_GPIO   GPIO_NUM_5

// -----------------------------------------------------------------------------
// LoRa modem parameter enums (SX1276/77/78 register-level values)
// -----------------------------------------------------------------------------
typedef enum {
    LORA_SF6  = 6,
    LORA_SF7  = 7,
    LORA_SF8  = 8,
    LORA_SF9  = 9,
    LORA_SF10 = 10,
    LORA_SF11 = 11,
    LORA_SF12 = 12,
} lora_sf_t;

// Values are the 4-bit codes written into RegModemConfig1[7:4]
typedef enum {
    LORA_BW_7_8_KHZ   = 0,
    LORA_BW_10_4_KHZ  = 1,
    LORA_BW_15_6_KHZ  = 2,
    LORA_BW_20_8_KHZ  = 3,
    LORA_BW_31_25_KHZ = 4,
    LORA_BW_41_7_KHZ  = 5,
    LORA_BW_62_5_KHZ  = 6,
    LORA_BW_125_KHZ   = 7,
    LORA_BW_250_KHZ   = 8,
    LORA_BW_500_KHZ   = 9,
} lora_bw_t;

// Values are the 3-bit codes written into RegModemConfig1[3:1]
typedef enum {
    LORA_CR_4_5 = 1,
    LORA_CR_4_6 = 2,
    LORA_CR_4_7 = 3,
    LORA_CR_4_8 = 4,
} lora_cr_t;

/**
 * @brief Unified Configuration struct for LoRa parameters
 */
typedef struct {
    uint32_t frequency_hz;    // RF frequency in Hz (e.g., 915000000)
    int8_t power_dbm;         // Output power in dBm (~2-20 dBm)
    lora_sf_t sf;             // Spreading Factor
    lora_bw_t bw;             // Bandwidth
    lora_cr_t cr;             // Coding Rate
    uint16_t preamble_length; // Preamble length in symbols
    bool header_explicit;     // true = explicit header, false = implicit
    uint8_t payload_length;   // Max payload length / fixed length
    bool crc_on;              // true = CRC enabled
} lora_config_t;

/**
 * @brief Helper macro providing default initialization values
 */
#define LORA_CONFIG_DEFAULT() {                             \
    .frequency_hz    = 915000000,                           \
    .power_dbm       = 14,                                  \
    .sf              = LORA_SF12,                           \
    .bw              = LORA_BW_125_KHZ,                     \
    .cr              = LORA_CR_4_5,                         \
    .preamble_length = 8,                                   \
    .header_explicit = true,                                \
    .payload_length  = 255,                                 \
    .crc_on          = true                                 \
}

#ifdef __cplusplus
}
#endif

#endif // LORA_DEFS_H
