// Minimal display stubs to avoid initializing the TFT hardware while
// migrating the board. These functions intentionally do no SPI/GPIO work.
#include "display.h"
#include "esp_log.h"

static const char *TAG = "display";

extern "C" void display_init(void)
{
    ESP_LOGI(TAG, "display_init skipped (TFT disabled for new board)");
}

extern "C" void display_set_link_status(bool wifi_connected, bool mstp_connected)
{
    (void)wifi_connected;
    (void)mstp_connected;
}

extern "C" void display_update_values(float av1, float av2, float av3, float av4)
{
    (void)av1; (void)av2; (void)av3; (void)av4;
}

extern "C" void display_update_footer(unsigned int bacnet_device_id, const char *ip_address)
{
    (void)bacnet_device_id; (void)ip_address;
}
