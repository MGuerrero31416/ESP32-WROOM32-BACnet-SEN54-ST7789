#include "display.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <stdio.h>

static TFT_eSPI tft = TFT_eSPI();

// Display boundary constants for a 170x320 panel in landscape orientation.
#define DISP_X0    0
#define DISP_Y0    0
#define DISP_X1    319
#define DISP_Y1    169
#define DISP_WIDTH 320
#define DISP_HEIGHT 170

// Global horizontal guard offset for panels that clip at the left edge.
#define DISP_GLOBAL_X_OFFSET 8

// Slight inset to keep text away from the physical left edge.
#define DISP_TEXT_X (41 + DISP_GLOBAL_X_OFFSET)

// Top inset to keep the first row fully visible.
#define DISP_TEXT_Y 20

// Four-row layout for Temp, humidity, PM2.5, and VOC.
#define DISP_ROW_SPACING 25
#define DISP_VALUE_X (105 + DISP_GLOBAL_X_OFFSET)

// Display update region constants (avoid recalculation on every refresh)
#define VALUE_X         (115 + DISP_GLOBAL_X_OFFSET)
#define VALUE_Y         20
#define VALUE_WIDTH     135
#define VALUE_HEIGHT    24
#define ROW_SPACING     25

// Footer at the bottom of the landscape display for BACnet ID and IPv4.
#define FOOTER_Y        126
#define FOOTER_X        DISP_TEXT_X
#define FOOTER_WIDTH    DISP_WIDTH
#define FOOTER_HEIGHT   16

extern "C" void display_init(void)
{
    initArduino();

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    // Set text properties once; retained for all subsequent draws
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);

    // Draw labels in column at DISP_TEXT_X
    const char *labels[4] = { "Temp", "%HR", "PM2.5", "VOC" };
    for (int i = 0; i < 4; i++) {
        tft.setCursor(DISP_TEXT_X, DISP_TEXT_Y + i * DISP_ROW_SPACING);
        tft.print(labels[i]);
    }

    printf("Display initialized\n");
}

extern "C" void display_set_link_status(bool wifi_connected, bool mstp_connected)
{
    (void)wifi_connected;
    (void)mstp_connected;
}

extern "C" void display_update_values(float av1, float av2, float av3, float av4)
{
    // Clear all value display rows with a single call to minimize SPI traffic
    for (int i = 0; i < 4; i++) {
        tft.fillRect(VALUE_X, VALUE_Y + i * ROW_SPACING, VALUE_WIDTH, VALUE_HEIGHT, TFT_BLACK);
    }

    // Draw all 4 values using consolidated loop (avoids 4 redundant setTextColor/setTextSize calls)
    char buf[16];
    const float values[4] = { av1, av2, av3, av4 };
    const char *formats[4] = { "%.1f", "%.1f", "%.0f", "%.0f" };

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);

    for (int i = 0; i < 4; i++) {
        tft.setCursor(VALUE_X, VALUE_Y + i * ROW_SPACING);
        snprintf(buf, sizeof(buf), formats[i], values[i]);
        tft.print(buf);
    }
}

extern "C" void display_update_footer(
    unsigned int bacnet_device_id,
    unsigned int mstp_mac_address,
    const char *ip_address)
{
    char footer_text[40];
    static char last_footer_text[40] = {0};
    (void)ip_address;

    snprintf(
        footer_text,
        sizeof(footer_text),
        "ID: %u MAC: %u",
        bacnet_device_id,
        mstp_mac_address);

    if (strcmp(footer_text, last_footer_text) == 0) {
        return;
    }

    strncpy(last_footer_text, footer_text, sizeof(last_footer_text) - 1);
    last_footer_text[sizeof(last_footer_text) - 1] = '\0';

    tft.fillRect(0, FOOTER_Y, FOOTER_WIDTH, FOOTER_HEIGHT, TFT_BLACK);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.setTextSize(2);

    tft.setCursor(FOOTER_X, FOOTER_Y);
    tft.print(footer_text);

    // Restore primary style used for value rows.
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
}
