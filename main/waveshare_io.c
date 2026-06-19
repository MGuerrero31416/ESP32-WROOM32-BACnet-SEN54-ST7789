#include "waveshare_io.h"

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "waveshare_io";
static bool waveshare_initialized = false;
/* Runtime-detected TCA9554 I2C address (falls back to macro) */
static uint8_t tca_addr = WAVESHARE_TCA9554_ADDR;

/* TCA9554 register addresses */
enum {
    TCA9554_REG_INPUT = 0x00,
    TCA9554_REG_OUTPUT = 0x01,
    TCA9554_REG_POLARITY = 0x02,
    TCA9554_REG_CONFIG = 0x03
};

/* Map channels 0..7 to GPIO numbers for DI (defined in header constants) */
static const gpio_num_t di_map[8] = {
    WAVESHARE_DI_GPIO0,
    WAVESHARE_DI_GPIO1,
    WAVESHARE_DI_GPIO2,
    WAVESHARE_DI_GPIO3,
    WAVESHARE_DI_GPIO4,
    WAVESHARE_DI_GPIO5,
    WAVESHARE_DI_GPIO6,
    WAVESHARE_DI_GPIO7
};

/* Cached DO output register (LSB channel 0) */
static uint8_t do_cache = 0;

/* I2C port used */
#define WAVESHARE_I2C_PORT I2C_NUM_1
#define WAVESHARE_I2C_FREQ 400000

/* Low-level I2C write of a single register */
static esp_err_t tca_write_reg(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    esp_err_t err = i2c_master_start(cmd);
    if (err != ESP_OK) goto cleanup;
    err = i2c_master_write_byte(cmd, (tca_addr << 1) | I2C_MASTER_WRITE, true);
    if (err != ESP_OK) goto cleanup;
    err = i2c_master_write_byte(cmd, reg, true);
    if (err != ESP_OK) goto cleanup;
    err = i2c_master_write_byte(cmd, val, true);
    if (err != ESP_OK) goto cleanup;
    err = i2c_master_stop(cmd);
    if (err == ESP_OK) err = i2c_master_cmd_begin(WAVESHARE_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    /* Log write operations for debugging (include detected address) */
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TCA9554@0x%02X: write reg=0x%02X val=0x%02X", tca_addr, reg, val);
    } else {
        ESP_LOGW(TAG, "TCA9554@0x%02X: write reg=0x%02X val=0x%02X -> %s (cmd_link)",
                 tca_addr, reg, val, esp_err_to_name(err));
        /* Try a simpler high-level write as a fallback */
        uint8_t outbuf[2] = { reg, val };
        esp_err_t ferr = i2c_master_write_to_device(WAVESHARE_I2C_PORT, tca_addr,
                                                   outbuf, sizeof(outbuf), pdMS_TO_TICKS(100));
        if (ferr == ESP_OK) {
            ESP_LOGI(TAG, "TCA9554@0x%02X: fallback write succeeded reg=0x%02X val=0x%02X",
                     tca_addr, reg, val);
            err = ESP_OK;
        } else {
            ESP_LOGE(TAG, "TCA9554@0x%02X: fallback write failed -> %s",
                     tca_addr, esp_err_to_name(ferr));
        }
    }
cleanup:
    i2c_cmd_link_delete(cmd);
    return err;
}

/* Low-level I2C read of a single register */
static esp_err_t tca_read_reg(uint8_t reg, uint8_t *val)
{
    if (!val) return ESP_ERR_INVALID_ARG;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    esp_err_t err = i2c_master_start(cmd);
    if (err != ESP_OK) goto cleanup;
    err = i2c_master_write_byte(cmd, (tca_addr << 1) | I2C_MASTER_WRITE, true);
    if (err != ESP_OK) goto cleanup;
    err = i2c_master_write_byte(cmd, reg, true);
    if (err != ESP_OK) goto cleanup;
    err = i2c_master_start(cmd);
    if (err != ESP_OK) goto cleanup;
    err = i2c_master_write_byte(cmd, (tca_addr << 1) | I2C_MASTER_READ, true);
    if (err != ESP_OK) goto cleanup;
    err = i2c_master_read_byte(cmd, val, I2C_MASTER_NACK);
    if (err != ESP_OK) goto cleanup;
    err = i2c_master_stop(cmd);
    if (err == ESP_OK) err = i2c_master_cmd_begin(WAVESHARE_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    /* Log read operations for debugging (include detected address) */
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TCA9554@0x%02X: read reg=0x%02X val=0x%02X", tca_addr, reg, *val);
    } else {
        ESP_LOGW(TAG, "TCA9554@0x%02X: read reg=0x%02X -> %s", tca_addr, reg, esp_err_to_name(err));
    }
cleanup:
    i2c_cmd_link_delete(cmd);
    return err;
}

void waveshare_io_init(void)
{
    esp_err_t err;

    if (waveshare_initialized) {
        ESP_LOGI(TAG, "waveshare_io already initialized");
        return;
    }

    // I2C master configuration
    i2c_config_t cfg = {0};
    cfg.mode = I2C_MODE_MASTER;
    cfg.sda_io_num = (gpio_num_t)WAVESHARE_I2C_SDA_GPIO;
    cfg.scl_io_num = (gpio_num_t)WAVESHARE_I2C_SCL_GPIO;
    cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.master.clk_speed = WAVESHARE_I2C_FREQ;

    err = i2c_param_config(WAVESHARE_I2C_PORT, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return;
    }

    err = i2c_driver_install(WAVESHARE_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK; /* already installed by another component */
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2c_driver_install initial attempt failed: %s, trying i2c_driver_delete and retry",
                 esp_err_to_name(err));
        /* Try deleting any stale driver state and retry once */
        i2c_driver_delete(WAVESHARE_I2C_PORT);
        err = i2c_driver_install(WAVESHARE_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return;
    }

    /* Probe for TCA9554 address (0x20..0x27). If one responds, use it. */
    {
        uint8_t found = 0;
        uint8_t probe_val = 0;
        for (uint8_t a = 0x20; a <= 0x27; a++) {
            tca_addr = a;
            if (tca_read_reg(TCA9554_REG_CONFIG, &probe_val) == ESP_OK) {
                ESP_LOGI(TAG, "Detected TCA9554 at 0x%02X", a);
                found = 1;
                break;
            }
        }
        if (!found) {
            tca_addr = WAVESHARE_TCA9554_ADDR;
            ESP_LOGW(TAG, "No TCA9554 detected in 0x20-0x27, using default 0x%02X", tca_addr);
        }
    }

    // Initialize TCA9554: polarity=0, outputs=0, config=0 (all outputs)
    if (tca_write_reg(TCA9554_REG_POLARITY, 0x00) != ESP_OK) {
        ESP_LOGW(TAG, "tca polarity write failed");
    }

    /* Default to all outputs OFF (TCA outputs are active-low on this board) */
    do_cache = 0xFF;
    if (tca_write_reg(TCA9554_REG_OUTPUT, do_cache) != ESP_OK) {
        ESP_LOGW(TAG, "tca output write failed");
    }

    if (tca_write_reg(TCA9554_REG_CONFIG, 0x00) != ESP_OK) {
        ESP_LOGW(TAG, "tca config write failed");
    }

    // Read back config and output registers for verification
    uint8_t reg_cfg = 0, out = 0, pol = 0;
    if (tca_read_reg(TCA9554_REG_CONFIG, &reg_cfg) == ESP_OK) {
        ESP_LOGI(TAG, "TCA9554 CONFIG=0x%02X", reg_cfg);
    }
    if (tca_read_reg(TCA9554_REG_POLARITY, &pol) == ESP_OK) {
        ESP_LOGI(TAG, "TCA9554 POLARITY=0x%02X", pol);
    }
    if (tca_read_reg(TCA9554_REG_OUTPUT, &out) == ESP_OK) {
        ESP_LOGI(TAG, "TCA9554 OUTPUT=0x%02X", out);
    }
    ESP_LOGI(TAG, "TCA9554 initialized at 0x%02X", tca_addr);

    // Configure DI GPIOs as inputs with pull-ups
    gpio_config_t io_conf = {0};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    uint64_t mask = 0;
    for (int i = 0; i < 8; i++) mask |= (1ULL << di_map[i]);
    io_conf.pin_bit_mask = mask;
    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGW(TAG, "DI gpio_config failed");
    }

    ESP_LOGI(TAG, "DI GPIOs configured (channels 0..7)");

    waveshare_initialized = true;
}

bool waveshare_read_di(uint8_t channel)
{
    if (channel > 7) return false;
    gpio_num_t g = di_map[channel];
    int level = gpio_get_level(g);
    /* DI inputs are active-low on this board: return true when GPIO reads low */
    return level == 0;
}

void waveshare_write_do(uint8_t channel, bool state)
{
    if (channel > 7) return;
    uint8_t bit = (1u << channel);
    /* Hardware outputs are active-low: logical ON -> clear bit (0), OFF -> set bit (1) */
    if (state) do_cache &= ~bit; else do_cache |= bit;
    ESP_LOGI(TAG, "TCA9554 write: DO%d <= %u (OUT=0x%02X)", (int)channel+1, state ? 1 : 0, do_cache);
    esp_err_t err = tca_write_reg(TCA9554_REG_OUTPUT, do_cache);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write DO channel %u -> %s", (unsigned)channel, esp_err_to_name(err));
    }
}

bool waveshare_read_do(uint8_t channel)
{
    if (channel > 7) return false;
    uint8_t out = 0;
    if (tca_read_reg(TCA9554_REG_OUTPUT, &out) == ESP_OK) {
        do_cache = out;
    }
    /* Return logical state: true when output is ON (active-low, bit==0) */
    return (do_cache & (1u << channel)) == 0;
}

void test_outputs(void)
{
    for (uint8_t ch = 0; ch < 8; ch++) {
        ESP_LOGI(TAG, "Testing DO%u", (unsigned)(ch + 1));
        ESP_LOGI(TAG, "test_outputs: setting DO%u ON", (unsigned)(ch + 1));
        waveshare_write_do(ch, true);
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "test_outputs: setting DO%u OFF", (unsigned)(ch + 1));
        waveshare_write_do(ch, false);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
