#include "lora.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"

// SWSD003 Library Headers
#include "sx126x.h"
#include "sx126x_hal.h"

static const char *TAG = "lora_driver";

static void lora_wait_busy(void) {
    while (gpio_get_level(LORA_BUSY_GPIO) == 1) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// -----------------------------------------------------------------------------
// SWSD003 Low-Level HAL Functions
// -----------------------------------------------------------------------------
sx126x_hal_status_t sx126x_hal_write(const void* context, const uint8_t* command, 
                                     const uint16_t command_length, const uint8_t* data, 
                                     const uint16_t data_length) {
    lora_wait_busy();
    gpio_set_level(LORA_NSS_GPIO, 0);

    spi_trans_t trans = {0};
    trans.mosi = (void*)command;
    trans.bits.mosi = command_length * 8;
    spi_trans(HSPI_HOST, &trans);

    if (data_length > 0 && data != NULL) {
        memset(&trans, 0, sizeof(trans));
        trans.mosi = (void*)data;
        trans.bits.mosi = data_length * 8;
        spi_trans(HSPI_HOST, &trans);
    }

    gpio_set_level(LORA_NSS_GPIO, 1);
    lora_wait_busy();
    return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_read(const void* context, const uint8_t* command, 
                                    const uint16_t command_length, uint8_t* data, 
                                    const uint16_t data_length) {
    lora_wait_busy();
    gpio_set_level(LORA_NSS_GPIO, 0);

    spi_trans_t trans = {0};
    trans.mosi = (void*)command;
    trans.bits.mosi = command_length * 8;
    spi_trans(HSPI_HOST, &trans);

    if (data_length > 0 && data != NULL) {
        memset(&trans, 0, sizeof(trans));
        trans.miso = data;
        trans.bits.miso = data_length * 8;
        spi_trans(HSPI_HOST, &trans);
    }

    gpio_set_level(LORA_NSS_GPIO, 1);
    lora_wait_busy();
    return SX126X_HAL_STATUS_OK;
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

    gpio_config_t busy_conf = {
        .pin_bit_mask = (1ULL << LORA_BUSY_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&busy_conf);

    gpio_set_level(LORA_NSS_GPIO, 1);

    // 2. Perform Hardware Reset Sequence
    gpio_set_level(LORA_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LORA_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 3. Initialize SPI Bus
    spi_config_t spi_conf = {
        .interface = { .cs_en = 0 },
        .clk_div = SPI_2MHz_DIV,
    };
    spi_init(HSPI_HOST, &spi_conf);

    lora_wait_busy();

    // 4. Set Standby Mode (STDBY_RC)
    if (sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC) != SX126X_STATUS_OK) {
        LOGE(TAG, "Failed to put SX1262 into standby mode.");
        return false;
    }

    // 5. Ra-01SH Module Specifics: Set LDO Mode and RF Switch Control
    sx126x_set_reg_mode(NULL, SX126X_REG_MODE_LDO);
    sx126x_set_dio2_as_rf_sw_ctrl(NULL, true);

    // 6. Set Radio to LoRa Packet Type
    sx126x_set_pkt_type(NULL, SX126X_PKT_TYPE_LORA);

    // 7. Set RF Frequency (e.g. 915 MHz)
    sx126x_set_rf_freq(NULL, config->frequency_hz);

    // 8. Configure Power Amplifier (PA) & Tx Output Power for SX1262 (+22 dBm max chip)
    // Duty cycle = 0x04, hp_max = 0x07, device_sel = 0x00 (SX1262), pa_lut = 0x01
    sx126x_pa_cfg_params_t pa_cfg = {
        .pa_duty_cycle = 0x04,
        .hp_max        = 0x07,
        .device_sel    = 0x00,
        .pa_lut        = 0x01
    };
    sx126x_set_pa_cfg(NULL, &pa_cfg);
    sx126x_set_tx_params(NULL, config->power_dbm, SX126X_RAMP_20_US);

    // 9. Configure Modulation Parameters (SF12, BW125, CR4/5, Low Data Rate Optimization)
    // Note: LDRO (Low Data Rate Optimization) must be enabled for SF11/SF12 with 125kHz BW
    bool ldro_enable = (config->sf == SX126X_LORA_SF11 || config->sf == SX126X_LORA_SF12) && 
                       (config->bw == SX126X_LORA_BW_125);

    sx126x_mod_params_lora_t mod_params = {
        .sf   = config->sf,
        .bw   = config->bw,
        .cr   = config->cr,
        .ldro = ldro_enable ? 1 : 0
    };
    sx126x_set_lora_mod_params(NULL, &mod_params);

    // 10. Configure Packet Parameters
    sx126x_pkt_params_lora_t pkt_params = {
        .preamble_len_in_symb = config->preamble_length,
        .header_type          = config->header_explicit ? SX126X_LORA_PKT_EXPLICIT : SX126X_LORA_PKT_IMPLICIT,
        .pld_len_in_bytes     = config->payload_length,
        .crc_is_on            = config->crc_on,
        .invert_iq_is_on      = config->invert_iq
    };
    sx126x_set_lora_pkt_params(NULL, &pkt_params);

    // 11. Set LoRa Sync Word (0x1424 = Public Network / LoRaWAN, 0x12 = Private Network default)
    sx126x_set_lora_sync_word(NULL, 0x12);

    LOGI(TAG, "Ra-01SH configured: Freq=%d Hz, Power=%d dBm, SF=%d, BW=%d, CR=%d",
             config->frequency_hz, config->power_dbm, config->sf, config->bw, config->cr);

    return true;
}

bool lora_check_connection(void) {
    sx126x_chip_status_t status;
    if (sx126x_get_status(NULL, &status) == SX126X_STATUS_OK) {
        LOGI(TAG, "Ra-01SH status check OK! Mode: 0x%02X, Cmd Status: 0x%02X", 
                 status.chip_mode, status.cmd_status);
        return true;
    }
    LOGE(TAG, "Failed communication with Ra-01SH module.");
    return false;
}

// -----------------------------------------------------------------------------
// Read / Write Packet API
// -----------------------------------------------------------------------------
bool lora_send_packet(const uint8_t *data, uint8_t length, uint32_t timeout_ms) {
    if (data == NULL || length == 0) return false;

    // Clear IRQ flags
    sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL);

    // Write payload into chip FIFO at buffer offset 0x00
    if (sx126x_write_buffer(NULL, 0x00, data, length) != SX126X_STATUS_OK) {
        LOGE(TAG, "Failed to write payload to SX1262 buffer.");
        return false;
    }

    // Set Tx mode (Timeout = 0 means wait indefinitely in hardware until transmit finishes)
    if (sx126x_set_tx(NULL, 0) != SX126X_STATUS_OK) {
        LOGE(TAG, "Failed to put radio in TX mode.");
        return false;
    }

    // Poll until TxDone IRQ status flag is set or timeout occurs
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        sx126x_irq_mask_t irq_status;
        sx126x_get_irq_status(NULL, &irq_status);

        if (irq_status & SX126X_IRQ_TX_DONE) {
            sx126x_clear_irq_status(NULL, SX126X_IRQ_TX_DONE);
            LOGI(TAG, "Packet sent successfully (%d bytes).", length);
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
    }

    LOGE(TAG, "TX timeout expired!");
    sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC); // Reset radio state
    return false;
}

bool lora_receive_packet(uint8_t *buffer, uint8_t max_length, uint8_t *rx_length, uint32_t timeout_ms) {
    if (buffer == NULL || rx_length == NULL) return false;

    sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL);

    // Put radio in continuous RX mode (Timeout parameter 0xFFFFFF)
    if (sx126x_set_rx(NULL, 0xFFFFFF) != SX126X_STATUS_OK) {
        LOGE(TAG, "Failed to set RX mode.");
        return false;
    }

    uint32_t elapsed = 0;
    while (elapsed <= timeout_ms) {
        sx126x_irq_mask_t irq_status;
        sx126x_get_irq_status(NULL, &irq_status);

        if (irq_status & SX126X_IRQ_RX_DONE) {
            if (irq_status & SX126X_IRQ_CRC_ERROR) {
                LOGE(TAG, "Received packet CRC error!");
                sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL);
                return false;
            }

            // Get received payload info
            sx126x_rx_buffer_status_t rx_buffer_status;
            sx126x_get_rx_buffer_status(NULL, &rx_buffer_status);

            uint8_t bytes_to_read = rx_buffer_status.pld_len_in_bytes;
            if (bytes_to_read > max_length) {
                bytes_to_read = max_length;
            }

            // Read payload from chip internal buffer
            sx126x_read_buffer(NULL, rx_buffer_status.buffer_start_pointer, buffer, bytes_to_read);
            *rx_length = bytes_to_read;

            sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL);
            LOGI(TAG, "Received packet (%d bytes).", bytes_to_read);
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
    }

    // Standby radio if timeout expired with no packet
    sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC);
    return false;
}
