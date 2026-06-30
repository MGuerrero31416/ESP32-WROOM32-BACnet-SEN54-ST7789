#include "sen54.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SEN54";
static bool i2c_ready = false;
static i2c_master_bus_handle_t sen54_i2c_bus = NULL;
static i2c_master_dev_handle_t sen54_i2c_dev = NULL;

// SEN54 I2C commands (big-endian 16-bit)
// 0x0021: Start Measurement (enables fan and continuous measurements)
// 0x0104: Stop Measurement
// 0x03C4: Read Measured Values
// 0xD304: Device Reset (hardware reset, clears all learned state)
#define SEN54_CMD_START_MEASUREMENT  0x0021
#define SEN54_CMD_STOP_MEASUREMENT   0x0104
#define SEN54_CMD_READ_VALUES        0x03C4
// Device Reset (0xD304): forces a full hardware reset of the SEN54.
// All internal state — including the VOC/NOx algorithm learned baselines —
// is cleared. The sensor re-runs its start-up sequence (~1 s) and requires
// a new Start Measurement command (0x0021) before readings resume.
// Equivalent to a power-cycle. See SEN54 datasheet §3.2 "Device Reset".
#define SEN54_CMD_RESET              0xD304

// Read Measured Values returns 8 words × 3 bytes (2 data + 1 CRC) = 24 bytes
#define SEN54_READ_VALUES_LEN  24

static sen54_data_t current_data = {
    .pm1_0 = -1.0f, .pm2_5 = -1.0f, .pm4_0 = -1.0f, .pm10 = -1.0f,
    .humidity = -1.0f, .temperature = -1.0f, .voc_index = -1.0f, .nox_index = -1.0f
};
static SemaphoreHandle_t sen54_mutex = NULL;

// ---------------------------------------------------------------------------
// CRC-8 (polynomial 0x31, init 0xFF) used by all Sensirion sensors
// ---------------------------------------------------------------------------
static uint8_t sen54_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Low-level I2C helpers
// ---------------------------------------------------------------------------
static esp_err_t sen54_write_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    if (!sen54_i2c_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(sen54_i2c_dev, buf, sizeof(buf), 100);
}

static esp_err_t sen54_read_bytes(uint8_t *buf, size_t len)
{
    if (!sen54_i2c_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_receive(sen54_i2c_dev, buf, len, 100);
}

// ---------------------------------------------------------------------------
// Parse a word (2 bytes) from a 3-byte CRC-word group, validating CRC.
// Returns false if CRC fails.
// ---------------------------------------------------------------------------
static bool sen54_parse_word(const uint8_t *p, uint16_t *out)
{
    if (sen54_crc8(p, 2) != p[2]) {
        return false;
    }
    *out = ((uint16_t)p[0] << 8) | p[1];
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void sen54_init(void)
{
    esp_err_t err = ESP_OK;
    if (!i2c_ready) {
        const i2c_master_bus_config_t bus_cfg = {
            .i2c_port = SEN54_I2C_PORT,
            .sda_io_num = SEN54_I2C_SDA_PIN,
            .scl_io_num = SEN54_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags.enable_internal_pullup = 1,
        };
        err = i2c_new_master_bus(&bus_cfg, &sen54_i2c_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
            return;
        }

        const i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = SEN54_I2C_ADDR,
            .scl_speed_hz = SEN54_I2C_FREQ_HZ,
        };
        err = i2c_master_bus_add_device(sen54_i2c_bus, &dev_cfg, &sen54_i2c_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
            return;
        }

        i2c_ready = true;
    }

    // SEN54 needs up to 1 second after power-on before it will ACK on I2C
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Retry start measurement — cold power-up can take several seconds.
    err = ESP_FAIL;
    for (int attempt = 1; attempt <= 20; attempt++) {
        err = sen54_write_cmd(SEN54_CMD_START_MEASUREMENT);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "SEN54 initialized (attempt %d), measurement started", attempt);

            vTaskDelay(pdMS_TO_TICKS(100));

                            sen54_data_t test_data;
                            bool ok = sen54_read(&test_data);
                            ESP_LOGI(TAG, "First read result = %s", ok ? "OK" : "FAILED");

            break;
        }
    //    ESP_LOGW(TAG, "Start measurement attempt %d/20 failed: %s — retrying in 500 ms",
    //             attempt, esp_err_to_name(err));
    //    vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SEN54 not responding at I2C addr 0x%02X (SDA=GPIO%d, SCL=GPIO%d). "
                 "Check wiring and pull-up resistors.",
                 SEN54_I2C_ADDR, SEN54_I2C_SDA_PIN, SEN54_I2C_SCL_PIN);
    }
}

bool sen54_read(sen54_data_t *data)
{
    if (!data) {
        return false;
    }

    // Issue the Read Measured Values command
    esp_err_t err = sen54_write_cmd(SEN54_CMD_READ_VALUES);
    if (err != ESP_OK) {
        // If startup was missed after a cold boot, restart measurement and retry once.
        if (sen54_write_cmd(SEN54_CMD_START_MEASUREMENT) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            err = sen54_write_cmd(SEN54_CMD_READ_VALUES);
        }
        if (err != ESP_OK) {
            return false;
        }
    }

    // Sensor needs a short time to prepare the response
    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t buf[SEN54_READ_VALUES_LEN];
    err = sen54_read_bytes(buf, sizeof(buf));
    if (err != ESP_OK) {
        return false;
    }

    // Each word occupies 3 bytes: byte[0], byte[1], CRC
    uint16_t raw_pm1_0, raw_pm2_5, raw_pm4_0, raw_pm10;
    uint16_t raw_hum, raw_temp, raw_voc, raw_nox;

    if (!sen54_parse_word(&buf[0],  &raw_pm1_0) ||
        !sen54_parse_word(&buf[3],  &raw_pm2_5) ||
        !sen54_parse_word(&buf[6],  &raw_pm4_0) ||
        !sen54_parse_word(&buf[9],  &raw_pm10)  ||
        !sen54_parse_word(&buf[12], &raw_hum)   ||
        !sen54_parse_word(&buf[15], &raw_temp)  ||
        !sen54_parse_word(&buf[18], &raw_voc)   ||
        !sen54_parse_word(&buf[21], &raw_nox)) {
        ESP_LOGW(TAG, "CRC error in measurement response");
        return false;
    }

    // Apply scaling factors per SEN54 datasheet
    data->pm1_0        = (raw_pm1_0 == 0xFFFF) ? -1.0f : (float)raw_pm1_0 / 10.0f;
    data->pm2_5        = (raw_pm2_5 == 0xFFFF) ? -1.0f : (float)raw_pm2_5 / 10.0f;
    data->pm4_0        = (raw_pm4_0 == 0xFFFF) ? -1.0f : (float)raw_pm4_0 / 10.0f;
    data->pm10         = (raw_pm10  == 0xFFFF) ? -1.0f : (float)raw_pm10  / 10.0f;
    data->humidity     = ((int16_t)raw_hum  == 0x7FFF) ? -1.0f : (float)(int16_t)raw_hum  / 100.0f;
    data->temperature  = ((int16_t)raw_temp == 0x7FFF) ? -1.0f : (float)(int16_t)raw_temp / 200.0f;
    data->voc_index    = ((int16_t)raw_voc  == 0x7FFF) ? -1.0f : (float)(int16_t)raw_voc  / 10.0f;
    data->nox_index    = ((int16_t)raw_nox  == 0x7FFF) ? -1.0f : (float)(int16_t)raw_nox  / 10.0f;

    return true;
}

static void sen54_read_task(void *pvParameters)
{
    uint32_t interval_ms = (uint32_t)(uintptr_t)pvParameters;
    sen54_data_t data;

    for (;;) {
        if (sen54_read(&data)) {
            if (sen54_mutex && xSemaphoreTake(sen54_mutex, portMAX_DELAY) == pdTRUE) {
                memcpy(&current_data, &data, sizeof(sen54_data_t));
                xSemaphoreGive(sen54_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
}

void sen54_start_task(uint32_t interval_ms)
{
    if (interval_ms < 1000) {
        interval_ms = 1000;
    }
    if (!sen54_mutex) {
        sen54_mutex = xSemaphoreCreateMutex();
    }
    xTaskCreate(sen54_read_task, "sen54_task", 4096,
                (void *)(uintptr_t)interval_ms, 5, NULL);
}

// ---------------------------------------------------------------------------
// Thread-safe getters
// ---------------------------------------------------------------------------
#define SEN54_GETTER(field) \
    float value = -1.0f; \
    if (sen54_mutex && xSemaphoreTake(sen54_mutex, portMAX_DELAY) == pdTRUE) { \
        value = current_data.field; \
        xSemaphoreGive(sen54_mutex); \
    } else { \
        value = current_data.field; \
    } \
    return value;

float sen54_get_pm1_0(void)       { SEN54_GETTER(pm1_0) }
float sen54_get_pm2_5(void)       { SEN54_GETTER(pm2_5) }
float sen54_get_pm4_0(void)       { SEN54_GETTER(pm4_0) }
float sen54_get_pm10(void)        { SEN54_GETTER(pm10) }
float sen54_get_humidity(void)    { SEN54_GETTER(humidity) }
float sen54_get_temperature(void) { SEN54_GETTER(temperature) }
float sen54_get_voc_index(void)   { SEN54_GETTER(voc_index) }
float sen54_get_nox_index(void)   { SEN54_GETTER(nox_index) }

esp_err_t sen54_full_reset(void)
{
    // Send I2C command 0xD304 (Device Reset).
    // This clears all sensor state including VOC/NOx algorithm baselines.
    // The sensor needs ~1 s to complete its start-up sequence before it
    // will ACK further commands, after which measurement is restarted.
    esp_err_t ret = sen54_write_cmd(SEN54_CMD_RESET);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SEN54 full reset command failed (%d)", ret);
        return ret;
    }
    ESP_LOGI(TAG, "SEN54 full reset (0xD304) sent");
    vTaskDelay(pdMS_TO_TICKS(1200));  /* datasheet: device ready after ~1 s */
    ret = sen54_write_cmd(SEN54_CMD_START_MEASUREMENT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SEN54 restart measurement after reset failed (%d)", ret);
    } else {
        ESP_LOGI(TAG, "SEN54 measurement restarted after full reset");
    }
    return ret;
}

void sen54_get_data(sen54_data_t *data)
{
    if (!data) {
        return;
    }
    if (sen54_mutex && xSemaphoreTake(sen54_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(data, &current_data, sizeof(sen54_data_t));
        xSemaphoreGive(sen54_mutex);
    } else {
        memcpy(data, &current_data, sizeof(sen54_data_t));
    }
}
