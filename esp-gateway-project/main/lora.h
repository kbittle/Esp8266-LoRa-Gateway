#ifndef LORA_H
#define LORA_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/spi.h"

// -----------------------------------------------------------------------------
// Hardware Pin Definitions for Ra-01H (SX1276/77/78-family module)
// -----------------------------------------------------------------------------
// This driver polls RegIrqFlags over SPI rather than watching a DIO
// interrupt pin, so only RST, NSS, and the SPI bus (MOSI/MISO/SCK) need to
// be wired. DIO0/DIO1 are intentionally not used/required.
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
 * @brief Configuration struct for LoRa parameters
 */
typedef struct {
    uint32_t frequency_hz;    // RF frequency in Hz (e.g., 915000000)
    int8_t power_dbm;         // Output power in dBm. ~2-17 dBm via PA_BOOST,
                               // 18-20 dBm engages high-power mode (PA_DAC).
    lora_sf_t sf;              // Spreading Factor
    lora_bw_t bw;              // Bandwidth
    lora_cr_t cr;              // Coding Rate
    uint16_t preamble_length; // Preamble length in symbols
    bool header_explicit;     // true = explicit header, false = implicit
    uint8_t payload_length;   // Max payload length (explicit) / fixed length (implicit)
    bool crc_on;               // true = CRC enabled
} lora_config_t;

/**
 * @brief Helper macro providing default initialization values (915 MHz, SF12, BW125, CR4/5)
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

// Function Declarations
bool lora_init(const lora_config_t *config);
bool lora_check_connection(void);
bool lora_send_packet(const uint8_t *data, uint8_t length, uint32_t timeout_ms);
bool lora_receive_packet(uint8_t *buffer, uint8_t max_length, uint8_t *rx_length, uint32_t timeout_ms);

#endif // LORA_H
