#include "lora.h"
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"
#include "esp8266/pin_mux_register.h"
#include "esp8266/gpio_register.h"

static const char *TAG = "lora_driver";

// -----------------------------------------------------------------------------
// SX1276/77/78 register map (subset used by this driver)
// -----------------------------------------------------------------------------
#define REG_FIFO                 0x00
#define REG_OP_MODE               0x01
#define REG_FRF_MSB               0x06
#define REG_FRF_MID               0x07
#define REG_FRF_LSB               0x08
#define REG_PA_CONFIG              0x09
#define REG_OCP                   0x0B
#define REG_LNA                   0x0C
#define REG_FIFO_ADDR_PTR          0x0D
#define REG_FIFO_TX_BASE_ADDR      0x0E
#define REG_FIFO_RX_BASE_ADDR      0x0F
#define REG_FIFO_RX_CURRENT_ADDR   0x10
#define REG_IRQ_FLAGS_MASK         0x11
#define REG_IRQ_FLAGS              0x12
#define REG_RX_NB_BYTES            0x13
#define REG_PKT_SNR_VALUE          0x19
#define REG_PKT_RSSI_VALUE         0x1A
#define REG_MODEM_CONFIG_1         0x1D
#define REG_MODEM_CONFIG_2         0x1E
#define REG_PREAMBLE_MSB           0x20
#define REG_PREAMBLE_LSB           0x21
#define REG_PAYLOAD_LENGTH         0x22
#define REG_MAX_PAYLOAD_LENGTH     0x23
#define REG_MODEM_CONFIG_3         0x26
#define REG_DETECTION_OPTIMIZE     0x31
#define REG_DETECTION_THRESHOLD    0x37
#define REG_SYNC_WORD              0x39
#define REG_DIO_MAPPING_1          0x40
#define REG_VERSION                0x42
#define REG_PA_DAC                 0x4D

// RegOpMode
#define LONG_RANGE_MODE       0x80
#define MODE_SLEEP             0x00
#define MODE_STDBY             0x01
#define MODE_TX                0x03
#define MODE_RX_CONTINUOUS     0x05

// RegPaConfig
#define PA_BOOST                0x80

// RegIrqFlags
#define IRQ_RX_TIMEOUT_MASK      0x80
#define IRQ_RX_DONE_MASK         0x40
#define IRQ_PAYLOAD_CRC_ERROR    0x20
#define IRQ_TX_DONE_MASK         0x08
#define IRQ_ALL_MASK             0xFF

#define SX1276_XTAL_FREQ  32000000.0
#define SX1276_FSTEP      (SX1276_XTAL_FREQ / 524288.0) // 2^19

// -----------------------------------------------------------------------------
// Low-level SPI register access
// -----------------------------------------------------------------------------
// NOTE: spi_trans_t.mosi/.miso are declared as uint32_t* by this driver, not
// uint8_t*/void*. To satisfy that type while keeping the exact same verified
// byte layout in memory, buffers below are declared as uint32_t and accessed
// byte-wise through a uint8_t* alias - this changes nothing at runtime, it
// only fixes the pointer type the compiler was warning about.

static uint8_t lora_read_reg(uint8_t addr) {
    gpio_set_level(LORA_NSS_GPIO, 0);

    // Single full-duplex transaction: byte 0 = address (read bit clear),
    // byte 1 = dummy out / real data in. Splitting this into two separate
    // spi_trans() calls is unreliable - do it as one transfer, same as writes.
    WORD_ALIGNED_ATTR uint32_t tx_word = 0;
    WORD_ALIGNED_ATTR uint32_t rx_word = 0;
    ((uint8_t *)&tx_word)[0] = addr & 0x7F;

    spi_trans_t trans = {0};
    trans.mosi = &tx_word;
    trans.miso = &rx_word;
    trans.bits.mosi = 8;
    trans.bits.miso = 8;
    spi_trans(HSPI_HOST, &trans);

    gpio_set_level(LORA_NSS_GPIO, 1);
    return ((uint8_t *)&rx_word)[0];
}

static void lora_write_reg(uint8_t addr, uint8_t value) {
    gpio_set_level(LORA_NSS_GPIO, 0);

    WORD_ALIGNED_ATTR uint32_t tx_word = 0;
    uint8_t *tx = (uint8_t *)&tx_word;
    tx[0] = addr | 0x80; // MSB=1 -> write
    tx[1] = value;

    spi_trans_t trans = {0};
    trans.mosi = &tx_word;
    trans.bits.mosi = 16;
    spi_trans(HSPI_HOST, &trans);

    gpio_set_level(LORA_NSS_GPIO, 1);
}

static void lora_write_fifo(const uint8_t *data, uint8_t length) {
    // Single transaction: address byte + payload, all on MOSI (no read-back needed for a write).
    static WORD_ALIGNED_ATTR uint32_t tx_words[64]; // 64*4 = 256 bytes
    uint8_t *tx = (uint8_t *)tx_words;
    tx[0] = REG_FIFO | 0x80;
    memcpy(&tx[1], data, length);

    gpio_set_level(LORA_NSS_GPIO, 0);
    spi_trans_t trans = {0};
    trans.mosi = tx_words;
    trans.bits.mosi = (length + 1) * 8;
    spi_trans(HSPI_HOST, &trans);
    gpio_set_level(LORA_NSS_GPIO, 1);
}

static void lora_read_fifo(uint8_t *data, uint8_t length) {
    // Same phase model as lora_read_reg: the mosi phase (address byte) and
    // miso phase (payload) are sequential, not simultaneous, so no dummy
    // padding byte or rx[] offset is needed - miso phase gives real data
    // starting at index 0.
    static WORD_ALIGNED_ATTR uint32_t tx_word = 0;
    static WORD_ALIGNED_ATTR uint32_t rx_words[64]; // 64*4 = 256 bytes
    ((uint8_t *)&tx_word)[0] = REG_FIFO & 0x7F;

    gpio_set_level(LORA_NSS_GPIO, 0);
    spi_trans_t trans = {0};
    trans.mosi = &tx_word;
    trans.miso = rx_words;
    trans.bits.mosi = 8;
    trans.bits.miso = length * 8;
    spi_trans(HSPI_HOST, &trans);
    gpio_set_level(LORA_NSS_GPIO, 1);

    memcpy(data, rx_words, length);
}

static void lora_set_mode(uint8_t mode) {
    lora_write_reg(REG_OP_MODE, LONG_RANGE_MODE | mode);
}

// -----------------------------------------------------------------------------
// Driver Initialization & Configuration
// -----------------------------------------------------------------------------
bool lora_init(const lora_config_t *config) {
    if (config == NULL) {
        LOGE(TAG, "Configuration struct is NULL.");
        return false;
    }

    // 1. Configure Hardware GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LORA_NSS_GPIO) | (1ULL << LORA_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    gpio_set_level(LORA_NSS_GPIO, 1);

    // 2. Hardware Reset Sequence (SX1276/78 needs RST low >=100us, then wait >=5ms)
    gpio_set_level(LORA_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LORA_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 3. Explicitly route GPIO12/13/14 to the HSPI peripheral (MISO/MOSI/CLK).
    // These pins default to plain GPIO on reset; if the IO mux isn't switched
    // to HSPI function (2), spi_trans() will never actually drive real clock/
    // data on the pins even though it reports success. Harmless/idempotent
    // if spi_init() below already does this itself.
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, 2); // GPIO12 -> HSPI MISO
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, 2); // GPIO13 -> HSPI MOSI
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, 2); // GPIO14 -> HSPI CLK

    // 4. Initialize SPI Bus
    spi_config_t spi_conf = {
        .interface = { .cs_en = 0 },
        .clk_div = SPI_2MHz_DIV,
    };
    spi_init(HSPI_HOST, &spi_conf);

    // 5. Verify chip identity - RegVersion should read back 0x12 on all SX1276/77/78/79 parts
    uint8_t version = lora_read_reg(REG_VERSION);
    if (version != 0x12) {
        LOGE(TAG, "Unexpected RegVersion 0x%02X (expected 0x12) - check wiring/chip.", version);
        return false;
    }

    // 6. Sleep -> LoRa mode must be selected while in SLEEP, then move to STANDBY
    lora_set_mode(MODE_SLEEP);
    vTaskDelay(pdMS_TO_TICKS(10));
    lora_set_mode(MODE_STDBY);

    // 7. Set RF Frequency
    uint64_t frf = (uint64_t)((double)config->frequency_hz / SX1276_FSTEP + 0.5);
    lora_write_reg(REG_FRF_MSB, (uint8_t)(frf >> 16));
    lora_write_reg(REG_FRF_MID, (uint8_t)(frf >> 8));
    lora_write_reg(REG_FRF_LSB, (uint8_t)(frf >> 0));

    // 8. Configure PA. Ra-01H routes RF out through PA_BOOST (not RFO).
    // 2-17 dBm: PaConfig only. 18-20 dBm: also enable PA_DAC high-power mode.
    int8_t power = config->power_dbm;
    if (power > 20) power = 20;
    if (power < 2) power = 2;

    if (power > 17) {
        lora_write_reg(REG_PA_DAC, 0x87); // +20dBm high power mode
        lora_write_reg(REG_PA_CONFIG, PA_BOOST | (uint8_t)(power - 5));
    } else {
        lora_write_reg(REG_PA_DAC, 0x84); // normal power mode
        lora_write_reg(REG_PA_CONFIG, PA_BOOST | (uint8_t)(power - 2));
    }
    lora_write_reg(REG_OCP, 0x20 | 0x0B); // OcpOn=1, ~100mA trim (safe default)

    // 9. LNA: max gain + LF/HF boost (standard recipe for sensitivity)
    lora_write_reg(REG_LNA, 0x23);

    // 10. FIFO base addresses - use full 256-byte FIFO for both TX and RX
    lora_write_reg(REG_FIFO_TX_BASE_ADDR, 0x00);
    lora_write_reg(REG_FIFO_RX_BASE_ADDR, 0x00);

    // 11. Modem Config 1: Bandwidth, Coding Rate, explicit/implicit header
    uint8_t mc1 = ((uint8_t)config->bw << 4) | ((uint8_t)config->cr << 1) |
                  (config->header_explicit ? 0 : 0x01);
    lora_write_reg(REG_MODEM_CONFIG_1, mc1);

    // 12. Modem Config 2: Spreading Factor, CRC
    uint8_t mc2 = ((uint8_t)config->sf << 4) | (config->crc_on ? 0x04 : 0x00);
    lora_write_reg(REG_MODEM_CONFIG_2, mc2);

    // 13. Modem Config 3: Low Data Rate Optimization (required for SF11/12 @ BW125) + AGC
    bool ldro = (config->sf == LORA_SF11 || config->sf == LORA_SF12) &&
                (config->bw == LORA_BW_125_KHZ);
    uint8_t mc3 = 0x04; // AgcAutoOn = 1
    if (ldro) mc3 |= 0x08;
    lora_write_reg(REG_MODEM_CONFIG_3, mc3);

    // 14. Detection optimize / threshold - standard values for SF7-SF12
    lora_write_reg(REG_DETECTION_OPTIMIZE, 0xC3);
    lora_write_reg(REG_DETECTION_THRESHOLD, 0x0A);

    // 15. Preamble length
    lora_write_reg(REG_PREAMBLE_MSB, (uint8_t)(config->preamble_length >> 8));
    lora_write_reg(REG_PREAMBLE_LSB, (uint8_t)(config->preamble_length & 0xFF));

    // 16. Payload length (used directly in implicit header mode; also sets max in explicit)
    lora_write_reg(REG_PAYLOAD_LENGTH, config->payload_length);
    lora_write_reg(REG_MAX_PAYLOAD_LENGTH, 0xFF);

    // 17. Sync word - 0x12 is the standard "private network" LoRa sync word
    lora_write_reg(REG_SYNC_WORD, 0x12);

    LOGI(TAG, "Ra-01H configured: Freq=%d Hz, Power=%d dBm, SF=%d, BW=%d, CR=%d",
             config->frequency_hz, config->power_dbm, config->sf, config->bw, config->cr);

    return true;
}

bool lora_check_connection(void) {
    uint8_t version = lora_read_reg(REG_VERSION);
    if (version == 0x12) {
        LOGI(TAG, "Ra-01H status check OK! RegVersion: 0x%02X", version);
        return true;
    }
    LOGE(TAG, "Failed communication with Ra-01H module (RegVersion read: 0x%02X).", version);
    return false;
}

// -----------------------------------------------------------------------------
// Read / Write Packet API
// -----------------------------------------------------------------------------
bool lora_send_packet(const uint8_t *data, uint8_t length, uint32_t timeout_ms) {
    if (data == NULL || length == 0) return false;

    lora_set_mode(MODE_STDBY);

    lora_write_reg(REG_FIFO_ADDR_PTR, 0x00);
    lora_write_fifo(data, length);
    lora_write_reg(REG_PAYLOAD_LENGTH, length);

    lora_write_reg(REG_IRQ_FLAGS, IRQ_ALL_MASK); // clear (write-1-to-clear)
    lora_set_mode(MODE_TX);

    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        uint8_t irq = lora_read_reg(REG_IRQ_FLAGS);
        if (irq & IRQ_TX_DONE_MASK) {
            lora_write_reg(REG_IRQ_FLAGS, IRQ_ALL_MASK);
            LOGI(TAG, "Packet sent successfully (%d bytes).", length);
            lora_set_mode(MODE_STDBY);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
    }

    LOGE(TAG, "TX timeout expired!");
    lora_write_reg(REG_IRQ_FLAGS, IRQ_ALL_MASK);
    lora_set_mode(MODE_STDBY);
    return false;
}

bool lora_receive_packet(uint8_t *buffer, uint8_t max_length, uint8_t *rx_length, uint32_t timeout_ms) {
    if (buffer == NULL || rx_length == NULL) return false;

    lora_set_mode(MODE_STDBY);
    lora_write_reg(REG_FIFO_ADDR_PTR, 0x00);
    lora_write_reg(REG_IRQ_FLAGS, IRQ_ALL_MASK);
    lora_set_mode(MODE_RX_CONTINUOUS);

    uint32_t elapsed = 0;
    while (elapsed <= timeout_ms) {
        uint8_t irq = lora_read_reg(REG_IRQ_FLAGS);

        if (irq & IRQ_RX_DONE_MASK) {
            if (irq & IRQ_PAYLOAD_CRC_ERROR) {
                LOGE(TAG, "Received packet CRC error!");
                lora_write_reg(REG_IRQ_FLAGS, IRQ_ALL_MASK);
                lora_set_mode(MODE_STDBY);
                return false;
            }

            uint8_t bytes_to_read = lora_read_reg(REG_RX_NB_BYTES);
            if (bytes_to_read > max_length) {
                bytes_to_read = max_length;
            }

            uint8_t fifo_addr = lora_read_reg(REG_FIFO_RX_CURRENT_ADDR);
            lora_write_reg(REG_FIFO_ADDR_PTR, fifo_addr);
            lora_read_fifo(buffer, bytes_to_read);
            *rx_length = bytes_to_read;

            lora_write_reg(REG_IRQ_FLAGS, IRQ_ALL_MASK);
            LOGI(TAG, "Received packet (%d bytes).", bytes_to_read);
            lora_set_mode(MODE_STDBY);
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
    }

    lora_write_reg(REG_IRQ_FLAGS, IRQ_ALL_MASK);
    lora_set_mode(MODE_STDBY);
    return false;
}
