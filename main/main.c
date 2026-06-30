/* Minimal example: connect to Wi‑Fi and initialize BACnet. */
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "wifi_helper.h"
#include "display.h"
#include "analog_value.h"
#include "binary_value.h"
#include "analog_input.h"
#include "binary_input.h"
#include "binary_output.h"
#include "sen54.h"
#include "mstp_rs485.h"
#include "User_Settings.h"

/* bacnet-stack headers */
#include "bacnet/basic/object/device.h"
#include "bacnet/basic/object/av.h"
#include "bacnet/basic/object/bv.h"
#include "bacnet/basic/object/ai.h"
#include "bacnet/basic/object/bi.h"
#include "bacnet/basic/object/bo.h"
#include "bacnet/basic/service/s_iam.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/bacaddr.h"
#include "bacnet/basic/bbmd/h_bbmd.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/bip.h"
#include "bacnet/datalink/dlmstp.h"
#include "bacnet/datalink/mstp.h"
/* service handlers from bacnet-stack library */
#include "bacnet/basic/services.h"
#include "bacnet/basic/service/h_rp.h"
#include "bacnet/basic/service/h_rpm.h"
#include "bacnet/basic/service/h_wp.h"
#include "bacnet/basic/service/h_whois.h"
#include "bacnet/basic/service/h_iam.h"
#include "bacnet/basic/service/h_cov.h"
#include "bacnet/basic/service/s_whois.h"
#include "bacnet/whois.h"
#include "bacnet/iam.h"
#include "bacnet/npdu.h"
#include "bacnet/basic/npdu/h_npdu.h"
#include "bacnet/bacenum.h"

static const char *TAG = "bacnet";

int override_nvs_on_flash = 0;  /* Exported for AV/BV modules */


static void bacnet_register_with_bbmd(void);
static void bacnet_receive_task(void *pvParameters);
static void bacnet_mstp_receive_task(void *pvParameters);
static void bacnet_cov_task(void *pvParameters);
static void sen54_task(void *pvParameters);
static TaskHandle_t bacnet_cov_task_handle = NULL;
static SemaphoreHandle_t bacnet_datalink_mutex = NULL;
static volatile uint32_t mstp_pdu_count = 0;
static volatile uint32_t mstp_apdu_count = 0;
static volatile uint32_t mstp_rp_total = 0;
static volatile uint32_t mstp_wp_total = 0;
static float mstp_rp_last_value = 0.0f;

static bool wifi_connected_now(void)
{
    if (!USER_ENABLE_BACNET_IP) {
        return false;
    }

    wifi_ap_record_t ap_info = {0};
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

static void wifi_ip_string_now(char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }

    out[0] = '\0';
    if (!USER_ENABLE_BACNET_IP) {
        snprintf(out, out_len, "No IP");
        return;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        snprintf(out, out_len, "No IP");
        return;
    }

    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(out, out_len, IPSTR, IP2STR(&ip_info.ip));
    } else {
        snprintf(out, out_len, "No IP");
    }
}

static void bacnet_log_whois_iam(const uint8_t *apdu, int apdu_len, const char *link)
{
    if (!apdu || apdu_len < 2) {
        return;
    }

    uint8_t pdu_type = apdu[0] & 0xF0;
    if (pdu_type != PDU_TYPE_UNCONFIRMED_SERVICE_REQUEST) {
        return;
    }

    uint8_t service_choice = apdu[1];
    if (service_choice == SERVICE_UNCONFIRMED_WHO_IS) {
        int32_t low_limit = -1;
        int32_t high_limit = -1;
        int len = whois_decode_service_request(
            &apdu[2], (unsigned)(apdu_len - 2), &low_limit, &high_limit);
        if (len >= 0) {
            bool in_range = true;
            if (low_limit >= 0 && high_limit >= 0) {
                uint32_t instance = USER_BACNET_DEVICE_INSTANCE;
                in_range = (instance >= (uint32_t)low_limit &&
                    instance <= (uint32_t)high_limit);
            }
            /*
            ESP_LOGI(
                TAG,
                "%s Who-Is low=%ld high=%ld local_instance=%lu match=%s",
                link,
                (long)low_limit,
                (long)high_limit,
                (unsigned long)USER_BACNET_DEVICE_INSTANCE,
                in_range ? "yes" : "no");
            */
            (void)in_range;
            (void)low_limit;
            (void)high_limit;
        } else {
            /* ESP_LOGW(TAG, "%s Who-Is decode failed len=%d", link, apdu_len); */
        }
    } else if (service_choice == SERVICE_UNCONFIRMED_I_AM) {
        uint32_t device_id = BACNET_MAX_INSTANCE;
        unsigned max_apdu = 0;
        int segmentation = SEGMENTATION_NONE;
        uint16_t vendor_id = 0;
        int len = iam_decode_service_request(
            &apdu[2], &device_id, &max_apdu, &segmentation, &vendor_id);
        if (len >= 0) {
            ESP_LOGI(
                TAG,
                "%s I-Am device=%lu vendor=%u max_apdu=%u",
                link,
                (unsigned long)device_id,
                (unsigned)vendor_id,
                max_apdu);
        } else {
            ESP_LOGW(TAG, "%s I-Am decode failed len=%d", link, apdu_len);
        }
    }
}

static char datalink_bip[] = "bip";
static char datalink_mstp[] = "mstp";
static char *datalink_default = NULL;

static uint8_t mstp_rx_buffer[512];
static uint8_t mstp_tx_buffer[512];
static struct mstp_port_struct_t mstp_port;
static struct dlmstp_user_data_t mstp_user;
static struct dlmstp_rs485_driver mstp_rs485_driver = {
    .init = MSTP_RS485_Init,
    .send = MSTP_RS485_Send,
    .read = MSTP_RS485_Read,
    .transmitting = MSTP_RS485_Transmitting,
    .baud_rate = MSTP_RS485_Baud_Rate,
    .baud_rate_set = MSTP_RS485_Baud_Rate_Set,
    .silence_milliseconds = MSTP_RS485_Silence_Milliseconds,
    .silence_reset = MSTP_RS485_Silence_Reset
};

static void bacnet_datalink_lock(char *name)
{
    if (bacnet_datalink_mutex) {
        xSemaphoreTake(bacnet_datalink_mutex, portMAX_DELAY);
    }
    datalink_set(name);
}

static void bacnet_datalink_unlock(void)
{
    if (datalink_default) {
        datalink_set(datalink_default);
    }
    if (bacnet_datalink_mutex) {
        xSemaphoreGive(bacnet_datalink_mutex);
    }
}

static bool bacnet_mstp_init(void)
{
    MSTP_RS485_Init();

    memset(&mstp_port, 0, sizeof(mstp_port));
    memset(&mstp_user, 0, sizeof(mstp_user));

    mstp_user.RS485_Driver = &mstp_rs485_driver;
    mstp_port.UserData = &mstp_user;
    mstp_port.InputBuffer = mstp_rx_buffer;
    mstp_port.InputBufferSize = sizeof(mstp_rx_buffer);
    mstp_port.OutputBuffer = mstp_tx_buffer;
    mstp_port.OutputBufferSize = sizeof(mstp_tx_buffer);

    dlmstp_set_interface((const char *)&mstp_port);
    dlmstp_set_mac_address(USER_MSTP_MAC_ADDRESS);
    dlmstp_set_max_info_frames(USER_MSTP_MAX_INFO_FRAMES);
    dlmstp_set_max_master(USER_MSTP_MAX_MASTER);
    dlmstp_set_baud_rate(USER_MSTP_BAUD_RATE);
    dlmstp_check_auto_baud_set(USER_MSTP_AUTO_BAUD);
    dlmstp_slave_mode_enabled_set(false);

    ESP_LOGI(
        TAG,
        "MS/TP config: mac=%u max_master=%u max_info=%u baud=%lu auto_baud=%s",
        (unsigned)USER_MSTP_MAC_ADDRESS,
        (unsigned)USER_MSTP_MAX_MASTER,
        (unsigned)USER_MSTP_MAX_INFO_FRAMES,
        (unsigned long)USER_MSTP_BAUD_RATE,
        USER_MSTP_AUTO_BAUD ? "on" : "off");

    return dlmstp_init((char *)&mstp_port);
}

/* BACnet receive task - processes incoming BACnet messages */
static void bacnet_receive_task(void *pvParameters)
{
    (void)pvParameters;
    BACNET_ADDRESS src = {0};
    static uint8_t rx_buffer[600];  /* Smaller buffer in DRAM */
    uint16_t pdu_len = 0;

    ESP_LOGI(TAG, "BACnet receive task started");

    while (1) {
        /* Poll for incoming BACnet messages */
        memset(&src, 0, sizeof(src));
        pdu_len = bip_receive(&src, rx_buffer, sizeof(rx_buffer), 100);
        if (pdu_len > 0) {
            /* Save original source from UDP socket before NPDU decode modifies it */
            BACNET_ADDRESS orig_src = src;
            BACNET_ADDRESS dest = {0};
            BACNET_NPDU_DATA npdu_data = {0};
            int apdu_offset = bacnet_npdu_decode(
                rx_buffer, pdu_len, &dest, &src, &npdu_data);
            /* If NPDU didn't have source routing info, restore from UDP socket */
            if (src.len == 0) {
                src = orig_src;
            }
            if (apdu_offset > 0 && apdu_offset < (int)pdu_len) {
                bacnet_log_whois_iam(&rx_buffer[apdu_offset], pdu_len - apdu_offset, "bip");
                bacnet_datalink_lock(datalink_bip);
                apdu_handler(&src, &rx_buffer[apdu_offset], pdu_len - apdu_offset);
                bacnet_datalink_unlock();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* BACnet MS/TP receive task - processes incoming MS/TP frames */
static void bacnet_mstp_receive_task(void *pvParameters)
{
    (void)pvParameters;
    BACNET_ADDRESS src = {0};
    static uint8_t rx_buffer[600];
    uint16_t pdu_len = 0;

    ESP_LOGI(TAG, "BACnet MS/TP receive task started");

    while (1) {
        memset(&src, 0, sizeof(src));
        pdu_len = dlmstp_receive(&src, rx_buffer, sizeof(rx_buffer), 0);
        if (pdu_len > 0) {
            mstp_pdu_count++;
            BACNET_ADDRESS dest = {0};
            BACNET_NPDU_DATA npdu_data = {0};
            int apdu_offset = bacnet_npdu_decode(
                rx_buffer, pdu_len, &dest, &src, &npdu_data);
            if (apdu_offset > 0 && apdu_offset < (int)pdu_len) {
                mstp_apdu_count++;
                if ((apdu_offset + 4) <= (int)pdu_len) {
                    uint8_t pdu_type = rx_buffer[apdu_offset] & 0xF0;
                    uint8_t service_choice = rx_buffer[apdu_offset + 3];
                    if (pdu_type == PDU_TYPE_CONFIRMED_SERVICE_REQUEST &&
                        service_choice == SERVICE_CONFIRMED_READ_PROPERTY) {
                        mstp_rp_total++;
                        mstp_rp_last_value = Analog_Value_Present_Value(1);
                    } else if (pdu_type == PDU_TYPE_CONFIRMED_SERVICE_REQUEST &&
                        service_choice == SERVICE_CONFIRMED_WRITE_PROPERTY) {
                        mstp_wp_total++;
                    }
                }
                bacnet_log_whois_iam(&rx_buffer[apdu_offset], pdu_len - apdu_offset, "mstp");
                bacnet_datalink_lock(datalink_mstp);
                apdu_handler(&src, &rx_buffer[apdu_offset], pdu_len - apdu_offset);
                bacnet_datalink_unlock();
            } else {
                ESP_LOGW(TAG, "MS/TP RX frame decode failed: len=%u apdu_offset=%d src.len=%u src.mac=%u",
                    (unsigned)pdu_len, apdu_offset, (unsigned)src.len,
                    (unsigned)(src.len ? src.mac[0] : 0));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* Application entry: init Wi‑Fi and BACnet */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    bacnet_datalink_mutex = xSemaphoreCreateMutex();
    if (!bacnet_datalink_mutex) {
        ESP_LOGE(TAG, "Failed to create BACnet datalink mutex");
    }
    
    /* If OVERRIDE_NVS_ON_FLASH is set, erase NVS to reset to code defaults.
     * Guard against wiping provisioned Wi-Fi when no compile-time defaults exist.
     */
    override_nvs_on_flash = USER_OVERRIDE_NVS_ON_FLASH;
    if (override_nvs_on_flash && USER_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "OVERRIDE_NVS_ON_FLASH ignored: USER_WIFI_SSID is empty and erase would remove provisioned Wi-Fi credentials");
        override_nvs_on_flash = 0;
    }

    if (override_nvs_on_flash) {
        ESP_LOGI(TAG, "Override flag set - erasing NVS to reset to defaults");
        nvs_flash_erase();
        ret = nvs_flash_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reinitialize NVS after erase: %d", ret);
        } else {
            ESP_LOGI(TAG, "NVS reinitialized successfully");
        }
    } else if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS needs initialization");
        nvs_flash_erase();
        nvs_flash_init();
    } else if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized from existing data");
    }

    User_Settings_Print();

    if (USER_ENABLE_BACNET_IP) {
        /* Initialize network stack (must be done before WiFi init) */
        esp_netif_init();
        esp_event_loop_create_default();

        wifi_init_sta();

        ESP_LOGI(TAG, "Initializing BACnet stack (B/IP)");
        datalink_set(datalink_bip);
        if (!datalink_init(NULL)) {
            ESP_LOGE(TAG, "Failed to initialize BACnet datalink");
            return;
        }

        bacnet_register_with_bbmd();
    }

    if (USER_ENABLE_BACNET_MSTP) {
        ESP_LOGI(TAG, "Initializing BACnet MS/TP");
        if (!bacnet_mstp_init()) {
            ESP_LOGE(TAG, "Failed to initialize BACnet MS/TP datalink");
        } else {
            datalink_set(datalink_mstp);
            if (!datalink_init((char *)&mstp_port)) {
                ESP_LOGE(TAG, "Failed to initialize BACnet MS/TP datalink interface");
            }
        }
    }

    if (USER_ENABLE_BACNET_IP) {
        datalink_default = datalink_bip;
    } else if (USER_ENABLE_BACNET_MSTP) {
        datalink_default = datalink_mstp;
    }
    if (datalink_default) {
        datalink_set(datalink_default);
    }

    Device_Init(NULL);
    User_Settings_InitDeviceIdentity();

    /* Register service handlers - using bacnet-stack library handlers */
    ESP_LOGI(TAG, "Registering BACnet service handlers");
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, handler_i_am_add);
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* Read Property - REQUIRED for BACnet devices */
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROP_MULTIPLE, handler_read_property_multiple);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, handler_write_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_SUBSCRIBE_COV, handler_cov_subscribe);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_SUBSCRIBE_COV_PROPERTY, handler_cov_subscribe_property);

    /* Initialize COV subscription list */
    handler_cov_init();

    /* Create BACnet objects (AV, BV, AI, BI, BO) */
    bacnet_create_analog_values();
    bacnet_create_binary_values();
    bacnet_create_analog_inputs();
    bacnet_create_binary_inputs();
    bacnet_create_binary_outputs_with_gpio_sync();  /* Create BO with GPIO sync task */

    ESP_LOGI(TAG, "Broadcasting I-Am");
    if (USER_ENABLE_BACNET_IP) {
        bacnet_datalink_lock(datalink_bip);
        Send_I_Am(Handler_Transmit_Buffer);
        bacnet_datalink_unlock();
    }
    if (USER_ENABLE_BACNET_MSTP) {
        bacnet_datalink_lock(datalink_mstp);
        Send_I_Am(Handler_Transmit_Buffer);
        bacnet_datalink_unlock();
    }

    /* Initialize display */
    ESP_LOGI(TAG, "Initializing display");
    display_init();

    /* Start BACnet receive task to handle incoming messages */
    if (USER_ENABLE_BACNET_IP) {
        if (xTaskCreate(bacnet_receive_task, "bacnet_rx", 16384, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create bacnet_rx task");
        }
    }
    if (USER_ENABLE_BACNET_MSTP) {
        if (xTaskCreate(bacnet_mstp_receive_task, "bacnet_mstp_rx", 12288, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create bacnet_mstp_rx task");
        }
    }
    if (xTaskCreate(bacnet_cov_task, "bacnet_cov", 24576, NULL, 4, &bacnet_cov_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create bacnet_cov task");
    }
    if (xTaskCreate(sen54_task, "sen54", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sen54 task");
    }

    if (USER_ENABLE_BACNET_MSTP) {
        ESP_LOGI(TAG, "BACnet MS/TP ready");
        dlmstp_reset_statistics();
    }

    /* Keep the task alive - maintenance + display updates */
    uint32_t display_tick = 0;
    uint32_t iam_tick = 0;
    uint32_t mstp_rx_tick = 0;
    uint32_t mstp_last_seen_pdu = 0;
    uint8_t mstp_alive_ticks = 0;
    while (1) {
        if (USER_ENABLE_BACNET_IP) {
            bacnet_datalink_lock(datalink_bip);
            datalink_maintenance_timer(1);
            bacnet_datalink_unlock();
        }

        if (USER_ENABLE_BACNET_MSTP && ++iam_tick % 60 == 0) {
            bacnet_datalink_lock(datalink_mstp);
            Send_I_Am(Handler_Transmit_Buffer);
            bacnet_datalink_unlock();
        }

        if (USER_ENABLE_BACNET_MSTP && ++mstp_rx_tick % 30 == 0) {
            uint32_t rx_bytes = MSTP_RS485_Rx_Bytes_Get_Reset();
            uint32_t preamble_55 = 0;
            uint32_t preamble_55ff = 0;
            struct dlmstp_statistics mstp_stats = {0};
            MSTP_RS485_Preamble_Counts_Get_Reset(&preamble_55, &preamble_55ff);
            dlmstp_fill_statistics(&mstp_stats);
#if !MSTP_DEBUG_ENABLE
            (void)rx_bytes;
            (void)preamble_55;
            (void)preamble_55ff;
#endif
            if (mstp_stats.bad_crc_counter > 0 ||
                mstp_stats.receive_invalid_frame_counter > 0 ||
                mstp_stats.lost_token_counter > 0) {
                ESP_LOGW(
                    TAG,
                    "MS/TP errors(30s): bad_crc=%lu invalid=%lu lost_token=%lu",
                    (unsigned long)mstp_stats.bad_crc_counter,
                    (unsigned long)mstp_stats.receive_invalid_frame_counter,
                    (unsigned long)mstp_stats.lost_token_counter);
            }
#if MSTP_DEBUG_ENABLE
            else {
                ESP_LOGD(
                    TAG,
                    "MS/TP 30s stats: rx_bytes=%lu preamble55=%lu preamble55ff=%lu pdu=%lu apdu=%lu rp=%lu wp=%lu valid=%lu invalid=%lu not_for_us=%lu bad_crc=%lu tx_frames=%lu rx_pdu=%lu poll=%lu lost_token=%lu sole_master=%d",
                    (unsigned long)rx_bytes,
                    (unsigned long)preamble_55,
                    (unsigned long)preamble_55ff,
                    (unsigned long)mstp_pdu_count,
                    (unsigned long)mstp_apdu_count,
                    (unsigned long)mstp_rp_total,
                    (unsigned long)mstp_wp_total,
                    (unsigned long)mstp_stats.receive_valid_frame_counter,
                    (unsigned long)mstp_stats.receive_invalid_frame_counter,
                    (unsigned long)mstp_stats.receive_valid_frame_not_for_us_counter,
                    (unsigned long)mstp_stats.bad_crc_counter,
                    (unsigned long)mstp_stats.transmit_frame_counter,
                    (unsigned long)mstp_stats.receive_pdu_counter,
                    (unsigned long)mstp_stats.poll_for_master_counter,
                    (unsigned long)mstp_stats.lost_token_counter,
                    dlmstp_sole_master() ? 1 : 0);
            }
#endif
            mstp_pdu_count = 0;
            mstp_apdu_count = 0;
            mstp_rp_total = 0;
            mstp_wp_total = 0;
            dlmstp_reset_statistics();
        }

        if (USER_ENABLE_BACNET_MSTP) {
            if (mstp_pdu_count != mstp_last_seen_pdu) {
                mstp_last_seen_pdu = mstp_pdu_count;
                mstp_alive_ticks = 6;
            } else if (mstp_alive_ticks > 0) {
                mstp_alive_ticks--;
            }
        } else {
            mstp_alive_ticks = 0;
        }

        display_set_link_status(
            wifi_connected_now(),
            USER_ENABLE_BACNET_MSTP && (mstp_alive_ticks > 0));
        
        /* Update display every 2 seconds */
        if (++display_tick % 2 == 0) {
            float av1 = Analog_Value_Present_Value(1);
            float av2 = Analog_Value_Present_Value(2);
            float av3 = Analog_Value_Present_Value(3);
            float av4 = Analog_Value_Present_Value(4);
            char ip_text[20];
            wifi_ip_string_now(ip_text, sizeof(ip_text));
            display_update_values(av1, av2, av3, av4);
            display_update_footer(
                USER_BACNET_DEVICE_INSTANCE,
                USER_MSTP_MAC_ADDRESS,
                ip_text);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* COV task - handles COV timer and notifications */
static void bacnet_cov_task(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        char *active_datalink = datalink_default;
        if (!active_datalink) {
            if (USER_ENABLE_BACNET_IP) {
                active_datalink = datalink_bip;
            } else if (USER_ENABLE_BACNET_MSTP) {
                active_datalink = datalink_mstp;
            }
        }

        if (active_datalink) {
            bacnet_datalink_lock(active_datalink);
            handler_cov_timer_seconds(1);
            handler_cov_task();
            bacnet_datalink_unlock();
        } else {
            handler_cov_timer_seconds(1);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* SEN54 task - reads sensor data and writes to BACnet Analog Value objects
 *
 * PERIPHERAL-TO-BACNET MAPPING:
 * - AV1 (instance 1): Temperature (°C)
 * - AV2 (instance 2): Relative Humidity (%RH)
 * - AV3 (instance 3): PM2.5 concentration (μg/m³)
 * - AV4 (instance 4): VOC Index (dimensionless)
 */
static void sen54_task(void *pvParameters)
{
    (void)pvParameters;
    sen54_data_t sensor_data;
    uint8_t consecutive_failures = 0;

    /* Give SEN54 time to boot after power-up */
    vTaskDelay(pdMS_TO_TICKS(3000));

    sen54_init();

    /* Wait for the sensor fan and particle chamber to stabilize */
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        /* BV1 written ACTIVE triggers a full SEN54 reset (I2C 0xD304) */
        if (Binary_Value_Present_Value(1) == BINARY_ACTIVE) {
            ESP_LOGI(TAG, "BV1 ACTIVE: sending SEN54 full reset");
            esp_err_t err = sen54_full_reset();
            ESP_LOGI(TAG, "SEN54 full reset %s", err == ESP_OK ? "OK" : "FAILED");
            Binary_Value_Present_Value_Set(1, BINARY_INACTIVE);
            continue;
        }

        if (sen54_read(&sensor_data)) {
            consecutive_failures = 0;
            Analog_Value_Present_Value_Set(1, sensor_data.temperature, 16);
            Analog_Value_Present_Value_Set(2, sensor_data.humidity,    16);
            Analog_Value_Present_Value_Set(3, sensor_data.pm2_5,       16);
            Analog_Value_Present_Value_Set(4, sensor_data.voc_index,   16);
            Analog_Value_Present_Value_Set(5, sensor_data.pm1_0,       16);
            Analog_Value_Present_Value_Set(6, sensor_data.pm4_0,       16);
            Analog_Value_Present_Value_Set(7, sensor_data.pm10,        16);
        } else {
            consecutive_failures++;

            /* -1 signals no valid data to BACnet clients */
            Analog_Value_Present_Value_Set(1, -1.0f, 16);
            Analog_Value_Present_Value_Set(2, -1.0f, 16);
            Analog_Value_Present_Value_Set(3, -1.0f, 16);
            Analog_Value_Present_Value_Set(4, -1.0f, 16);
            Analog_Value_Present_Value_Set(5, -1.0f, 16);
            Analog_Value_Present_Value_Set(6, -1.0f, 16);
            Analog_Value_Present_Value_Set(7, -1.0f, 16);

            if (consecutive_failures >= 5) {
                ESP_LOGW(TAG, "SEN54 read failed %u times, reinitializing sensor",
                         (unsigned)consecutive_failures);
                sen54_init();
                consecutive_failures = 0;
                vTaskDelay(pdMS_TO_TICKS(1500));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void bacnet_register_with_bbmd(void)
{
    BACNET_IP_ADDRESS bbmd_addr = { { USER_BBMD_IP_OCTET_1, USER_BBMD_IP_OCTET_2,
                                     USER_BBMD_IP_OCTET_3, USER_BBMD_IP_OCTET_4 },
                                    USER_BBMD_PORT };
    int result = bvlc_register_with_bbmd(&bbmd_addr, USER_BBMD_TTL_SECONDS);
    ESP_LOGI(TAG, "BBMD register result: %d", result);
}