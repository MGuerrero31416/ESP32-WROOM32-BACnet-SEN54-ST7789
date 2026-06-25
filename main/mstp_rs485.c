#include "mstp_rs485.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define MSTP_UART_PORT UART_NUM_2
#define MSTP_UART_TX_PIN GPIO_NUM_17
#define MSTP_UART_RX_PIN GPIO_NUM_16
#define MSTP_UART_DE_PIN GPIO_NUM_5
#define MSTP_UART_BAUD_DEFAULT 38400U
#define MSTP_UART_RX_BUF_SIZE 512
#define MSTP_UART_TX_BUF_SIZE 512
#define MSTP_UART_TX_TIMEOUT_MS 1000

static const char *TAG = "mstp_rs485";
static bool mstp_uart_initialized = false;
static volatile bool mstp_tx_in_progress = false;
static uint32_t mstp_baud_rate = MSTP_UART_BAUD_DEFAULT;
static int64_t mstp_last_activity_us = 0;
static volatile uint32_t mstp_rx_bytes = 0;
static volatile uint32_t mstp_preamble_55 = 0;
static volatile uint32_t mstp_preamble_55ff = 0;
static uint8_t mstp_prev_byte = 0;
static volatile uint32_t mstp_tx_frame_count = 0;

/* Rate-limit timestamps for TX control-frame logging (microseconds) */
static int64_t mstp_tx_token_log_us = 0;
static int64_t mstp_tx_pfm_log_us = 0;
static int64_t mstp_tx_rpfm_log_us = 0;
static volatile uint32_t mstp_tx_token_count = 0;
static volatile uint32_t mstp_tx_pfm_count = 0;
static volatile uint32_t mstp_tx_rpfm_count = 0;

static void mstp_rs485_set_tx_mode(bool enabled)
{
    gpio_set_level(MSTP_UART_DE_PIN, enabled ? 1 : 0);
}

void MSTP_RS485_Init(void)
{
    if (mstp_uart_initialized) {
        return;
    }

    uart_config_t config = {
        .baud_rate = (int)mstp_baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MSTP_UART_DE_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure DE pin: %d", err);
    }
    mstp_rs485_set_tx_mode(false);

    err = uart_param_config(MSTP_UART_PORT, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %d", err);
    }

    err = uart_set_pin(
        MSTP_UART_PORT, MSTP_UART_TX_PIN, MSTP_UART_RX_PIN,
        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %d", err);
    }

    err = uart_driver_install(
        MSTP_UART_PORT, MSTP_UART_RX_BUF_SIZE, MSTP_UART_TX_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %d", err);
    }

    mstp_last_activity_us = esp_timer_get_time();
    mstp_uart_initialized = true;

    ESP_LOGI(
        TAG,
        "MS/TP UART initialized on TX=%d RX=%d DE=%d @%lu baud",
        (int)MSTP_UART_TX_PIN,
        (int)MSTP_UART_RX_PIN,
        (int)MSTP_UART_DE_PIN,
        (unsigned long)mstp_baud_rate);
}

void MSTP_RS485_Send(const uint8_t *payload, uint16_t payload_len)
{
    if (!payload || payload_len == 0) {
        return;
    }
    if (!mstp_uart_initialized) {
        MSTP_RS485_Init();
    }

    /* --- TX diagnostics --- */
    mstp_tx_frame_count++;
    if (payload_len >= 8 && payload[0] == 0x55 && payload[1] == 0xFF) {
        uint8_t ftype = payload[2];
        uint8_t fdest = payload[3];
        uint8_t fsrc  = payload[4];
        uint16_t flen = ((uint16_t)payload[5] << 8) | payload[6];
        int64_t now_us = esp_timer_get_time();

        if (ftype == 0x00) { /* TOKEN */
            mstp_tx_token_count++;
            if ((now_us - mstp_tx_token_log_us) >= 5000000LL) {
                ESP_LOGI(TAG, "TX TOKEN dst=%u src=%u count=%lu",
                         fdest, fsrc, (unsigned long)mstp_tx_token_count);
                mstp_tx_token_log_us = now_us;
            }
        } else if (ftype == 0x01) { /* POLL_FOR_MASTER */
            mstp_tx_pfm_count++;
            if ((now_us - mstp_tx_pfm_log_us) >= 5000000LL) {
                ESP_LOGI(TAG, "TX POLL_FOR_MASTER dst=%u src=%u count=%lu",
                         fdest, fsrc, (unsigned long)mstp_tx_pfm_count);
                mstp_tx_pfm_log_us = now_us;
            }
        } else if (ftype == 0x02) { /* REPLY_TO_POLL_FOR_MASTER */
            mstp_tx_rpfm_count++;
            if ((now_us - mstp_tx_rpfm_log_us) >= 5000000LL) {
                ESP_LOGI(TAG, "TX REPLY_TO_PFM dst=%u src=%u count=%lu",
                         fdest, fsrc, (unsigned long)mstp_tx_rpfm_count);
                mstp_tx_rpfm_log_us = now_us;
            }
        } else {
            /* Data frames and other types: log every frame */
            ESP_LOGI(TAG, "TX frm#%lu type=0x%02X dst=%u src=%u dlen=%u plen=%u",
                     (unsigned long)mstp_tx_frame_count, ftype, fdest, fsrc,
                     flen, payload_len);
            /* Hex dump first 16 bytes */
            uint16_t dump_len = payload_len < 16u ? payload_len : 16u;
            char hex_buf[49]; /* 16*3 + 1 */
            for (uint16_t i = 0; i < dump_len; i++) {
                sprintf(&hex_buf[i * 3], "%02X ", payload[i]);
            }
            hex_buf[dump_len * 3 > 0 ? dump_len * 3 - 1 : 0] = '\0';
            ESP_LOGI(TAG, "TX hex[%u]: %s", dump_len, hex_buf);
        }
    } else {
        /* Malformed or raw frame */
        ESP_LOGW(TAG, "TX frm#%lu plen=%u (no preamble)",
                 (unsigned long)mstp_tx_frame_count, payload_len);
        uint16_t dump_len = payload_len < 16u ? payload_len : 16u;
        char hex_buf[49];
        for (uint16_t i = 0; i < dump_len; i++) {
            sprintf(&hex_buf[i * 3], "%02X ", payload[i]);
        }
        hex_buf[dump_len * 3 > 0 ? dump_len * 3 - 1 : 0] = '\0';
        ESP_LOGW(TAG, "TX hex[%u]: %s", dump_len, hex_buf);
    }
    /* --- end TX diagnostics --- */

    mstp_tx_in_progress = true;
    mstp_rs485_set_tx_mode(true);

    int written = uart_write_bytes(MSTP_UART_PORT, payload, payload_len);
    if (written < 0) {
        ESP_LOGE(TAG, "UART write failed");
    }

    uart_wait_tx_done(MSTP_UART_PORT, pdMS_TO_TICKS(MSTP_UART_TX_TIMEOUT_MS));
    mstp_rs485_set_tx_mode(false);
    mstp_tx_in_progress = false;
    mstp_last_activity_us = esp_timer_get_time();
}

bool MSTP_RS485_Read(uint8_t *buf)
{
    if (!buf) {
        return false;
    }
    if (!mstp_uart_initialized) {
        MSTP_RS485_Init();
    }

    int len = uart_read_bytes(MSTP_UART_PORT, buf, 1, 0);
    if (len > 0) {
        mstp_last_activity_us = esp_timer_get_time();
        mstp_rx_bytes += (uint32_t)len;
        if (*buf == 0x55) {
            mstp_preamble_55++;
        }
        if (mstp_prev_byte == 0x55 && *buf == 0xFF) {
            mstp_preamble_55ff++;
        }
        mstp_prev_byte = *buf;
        return true;
    }

    return false;
}

bool MSTP_RS485_Transmitting(void)
{
    return mstp_tx_in_progress;
}

uint32_t MSTP_RS485_Baud_Rate(void)
{
    return mstp_baud_rate;
}

bool MSTP_RS485_Baud_Rate_Set(uint32_t baud)
{
    if (baud == 0) {
        return false;
    }

    mstp_baud_rate = baud;
    if (mstp_uart_initialized) {
        esp_err_t err = uart_set_baudrate(MSTP_UART_PORT, (int)baud);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART set baud failed: %d", err);
            return false;
        }
    }

    return true;
}

uint32_t MSTP_RS485_Silence_Milliseconds(void)
{
    int64_t now_us = esp_timer_get_time();
    int64_t delta_us = now_us - mstp_last_activity_us;

    if (delta_us < 0) {
        return 0;
    }

    return (uint32_t)(delta_us / 1000);
}

void MSTP_RS485_Silence_Reset(void)
{
    mstp_last_activity_us = esp_timer_get_time();
}

uint32_t MSTP_RS485_Rx_Bytes_Get_Reset(void)
{
    uint32_t count = mstp_rx_bytes;
    mstp_rx_bytes = 0;
    return count;
}

void MSTP_RS485_Preamble_Counts_Get_Reset(uint32_t *preamble55, uint32_t *preamble55ff)
{
    if (preamble55) {
        *preamble55 = mstp_preamble_55;
    }
    if (preamble55ff) {
        *preamble55ff = mstp_preamble_55ff;
    }
    mstp_preamble_55 = 0;
    mstp_preamble_55ff = 0;
}

uint32_t MSTP_RS485_Tx_Frame_Count_Get_Reset(void)
{
    uint32_t count = mstp_tx_frame_count;
    mstp_tx_frame_count = 0;
    return count;
}
