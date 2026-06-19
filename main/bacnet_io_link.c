#include "bacnet_io_link.h"
#include "waveshare_io.h"
#include "User_Settings.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bacnet/basic/object/bi.h"
#include "bacnet/basic/object/bo.h"

static const char *TAG = "bacnet_io_link";

/* Last known states to avoid redundant updates */
static bool last_di_state[8];
static bool last_bo_state[8];

void bacnet_io_link_init(void)
{
    // Initialize hardware
    waveshare_io_init();

    // Initialize caches from hardware/BACnet
    for (uint8_t ch = 0; ch < 8; ch++) {
        last_di_state[ch] = waveshare_read_di(ch);
        // Initialize BACnet BI objects to current hardware state
        if (ch < USER_BI_COUNT) {
            uint32_t instance = USER_BI_INSTANCES[ch];
            Binary_Input_Present_Value_Set(instance, last_di_state[ch] ? BINARY_ACTIVE : BINARY_INACTIVE);
        }

        // Initialize BO-driven outputs according to BACnet present value
        if (ch < USER_BO_COUNT) {
            uint32_t bo_instance = USER_BO_INSTANCES[ch];
            BACNET_BINARY_PV pv = Binary_Output_Present_Value(bo_instance);
            bool state = (pv == BINARY_ACTIVE);
            last_bo_state[ch] = state;
            waveshare_write_do(ch, state);
        } else {
            last_bo_state[ch] = waveshare_read_do(ch);
        }
    }

    ESP_LOGI(TAG, "bacnet_io_link initialized");
}

/* This function is intended to be called periodically from the main loop.
 * It synchronizes physical DI->BACnet BI and BACnet BO->physical DO.
 */

/* FreeRTOS task: periodically sync DI -> BI and BO -> DO
 * Runs every 100 ms.
 */
void bacnet_io_link_task(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        /* DI -> BI: update Binary Input objects when hardware changes */
        for (uint8_t ch = 0; ch < 8; ch++) {
            bool hw = waveshare_read_di(ch);
            if (ch < USER_BI_COUNT) {
                uint32_t bi_instance = USER_BI_INSTANCES[ch];
                if (hw != last_di_state[ch]) {
                    last_di_state[ch] = hw;
                    Binary_Input_Present_Value_Set(bi_instance, hw ? BINARY_ACTIVE : BINARY_INACTIVE);
                }
            }
        }

        /* BO -> DO: read BACnet BO present values and drive hardware */
        for (uint8_t ch = 0; ch < 8; ch++) {
            if (ch < USER_BO_COUNT) {
                uint32_t bo_instance = USER_BO_INSTANCES[ch];
                BACNET_BINARY_PV pv = Binary_Output_Present_Value(bo_instance);
                bool desired = (pv == BINARY_ACTIVE);
                /* Only act and log when the desired state changes */
                if (desired != last_bo_state[ch]) {
                    last_bo_state[ch] = desired;
                    ESP_LOGI("bacnet_io", "BO%d=%d -> DO%d", (int)(ch + 1), desired ? 1 : 0, (int)(ch + 1));
                    waveshare_write_do(ch, desired);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* Read BO1..BO8 and drive DO1..DO8 with logging */
void sync_bacnet_outputs(void)
{
    for (uint8_t ch = 0; ch < 8; ch++) {
        if (ch < USER_BO_COUNT) {
            uint32_t bo_instance = USER_BO_INSTANCES[ch];
            BACNET_BINARY_PV pv = Binary_Output_Present_Value(bo_instance);
            bool desired = (pv == BINARY_ACTIVE);
            ESP_LOGI(TAG, "BO%u=%u -> DO%u %s", (unsigned)bo_instance, (unsigned)(pv == BINARY_ACTIVE),
                     (unsigned)(ch + 1), desired ? "ON" : "OFF");
            if (desired != last_bo_state[ch]) {
                last_bo_state[ch] = desired;
                waveshare_write_do(ch, desired);
            }
        }
    }
}
