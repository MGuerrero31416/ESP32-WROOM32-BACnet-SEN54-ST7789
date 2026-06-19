#include "User_Settings.h"
#include "bacnet/bacenum.h"

/* WiFi settings */
const bool USER_ENABLE_BACNET_IP = true;
const char USER_WIFI_SSID[] = "Gato_MG_2.4G";
const char USER_WIFI_PASS[] = "Hrt@31416";
const bool USER_WIFI_USE_STATIC_IP = false;
const char USER_WIFI_STATIC_IP_ADDR[] = "10.120.245.96";
const char USER_WIFI_STATIC_IP_GATEWAY[] = "10.210.245.254";
const char USER_WIFI_STATIC_IP_NETMASK[] = "255.255.255.0";

/* BACnet device settings */
const char USER_BACNET_DEVICE_NAME[] = "ESP32 (55501)";
const uint32_t USER_BACNET_DEVICE_INSTANCE = 55501;
const int USER_OVERRIDE_NVS_ON_FLASH = 0;

/* BBMD foreign device registration */
const uint8_t USER_BBMD_IP_OCTET_1 = 192;
const uint8_t USER_BBMD_IP_OCTET_2 = 168;
const uint8_t USER_BBMD_IP_OCTET_3 = 1;
const uint8_t USER_BBMD_IP_OCTET_4 = 1;
const uint16_t USER_BBMD_PORT = 0xBAC0;
const uint16_t USER_BBMD_TTL_SECONDS = 600;

/* BACnet MS/TP settings */
const bool USER_ENABLE_BACNET_MSTP = true;
const uint8_t USER_MSTP_MAC_ADDRESS = 21;
const uint8_t USER_MSTP_MAX_INFO_FRAMES = 80;
const uint8_t USER_MSTP_MAX_MASTER = 127;
const uint32_t USER_MSTP_BAUD_RATE = 38400U;

/* BACnet object defaults */
const uint32_t USER_AV_INSTANCES[USER_AV_COUNT] = { 1, 2, 3, 4, 5, 6, 7 };
const char *USER_AV_NAMES[USER_AV_COUNT] = {
    "Temp",
    "% RH",
    "PM2.5",
    "VOC Index",
    "PM1.0",
    "PM4",
    "PM10"
};
const char *USER_AV_DESCRIPTIONS[USER_AV_COUNT] = {
    "Temperature",
    "Humidity",
    "PM2.5",
    "VOC Index",
    "PM1.0",
    "PM4.0",
    "PM10"
};
const uint16_t USER_AV_UNITS[USER_AV_COUNT] = {
    UNITS_DEGREES_CELSIUS,
    UNITS_PERCENT,
    UNITS_MICROGRAMS_PER_CUBIC_METER,
    UNITS_NO_UNITS,
    UNITS_MICROGRAMS_PER_CUBIC_METER,
    UNITS_MICROGRAMS_PER_CUBIC_METER,
    UNITS_MICROGRAMS_PER_CUBIC_METER
};
const float USER_AV_INITIAL_VALUES[USER_AV_COUNT] = {
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f
};
const float USER_AV_COV_INCREMENTS[USER_AV_COUNT] = {
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f
};

const uint32_t USER_BV_INSTANCES[USER_BV_COUNT] = { 1, 2, 3, 4 };
const char *USER_BV_NAMES[USER_BV_COUNT] = {
    "SEN54_Full_Reset",
    "BV2",
    "BV3",
    "BV4"
};
const char *USER_BV_DESCRIPTIONS[USER_BV_COUNT] = {
    "Write ACTIVE to send I2C reset (0xD304) to SEN54",
    "Binary Value 2",
    "Binary Value 3",
    "Binary Value 4"
};
const char *USER_BV_ACTIVE_TEXT[USER_BV_COUNT] = {
    "RESETTING",
    "ACTIVE",
    "ACTIVE",
    "ACTIVE"
};
const char *USER_BV_INACTIVE_TEXT[USER_BV_COUNT] = {
    "IDLE",
    "INACTIVE",
    "INACTIVE",
    "INACTIVE"
};
const uint8_t USER_BV_INITIAL_VALUES[USER_BV_COUNT] = {
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_ACTIVE,
    BINARY_INACTIVE
};

const uint32_t USER_AI_INSTANCES[USER_AI_COUNT] = { 1, 2, 3, 4 };
const char *USER_AI_NAMES[USER_AI_COUNT] = {
    "AI1",
    "AI2",
    "AI3",
    "AI4"
};
const char *USER_AI_DESCRIPTIONS[USER_AI_COUNT] = {
    "Analog Input 1",
    "Analog Input 2",
    "Analog Input 3",
    "Analog Input 4"
};
const uint16_t USER_AI_UNITS[USER_AI_COUNT] = {
    UNITS_DEGREES_CELSIUS,
    UNITS_DEGREES_CELSIUS,
    UNITS_DEGREES_CELSIUS,
    UNITS_DEGREES_CELSIUS
};
const float USER_AI_INITIAL_VALUES[USER_AI_COUNT] = {
    0.0f,
    0.0f,
    0.0f,
    0.0f
};
const float USER_AI_COV_INCREMENTS[USER_AI_COUNT] = {
    1.0f,
    1.0f,
    1.0f,
    1.0f
};

const uint32_t USER_BI_INSTANCES[USER_BI_COUNT] = { 1, 2, 3, 4, 5, 6, 7, 8 };
const char *USER_BI_NAMES[USER_BI_COUNT] = {
    "BI1",
    "BI2",
    "BI3",
    "BI4",
    "BI5",
    "BI6",
    "BI7",
    "BI8"
};
const char *USER_BI_DESCRIPTIONS[USER_BI_COUNT] = {
    "Binary Input 1",
    "Binary Input 2",
    "Binary Input 3",
    "Binary Input 4",
    "Binary Input 5",
    "Binary Input 6",
    "Binary Input 7",
    "Binary Input 8"
};
const char *USER_BI_ACTIVE_TEXT[USER_BI_COUNT] = {
    "ACTIVE",
    "ACTIVE",
    "ACTIVE",
    "ACTIVE",
    "ACTIVE",
    "ACTIVE",
    "ACTIVE",
    "ACTIVE"
};
const char *USER_BI_INACTIVE_TEXT[USER_BI_COUNT] = {
    "INACTIVE",
    "INACTIVE",
    "INACTIVE",
    "INACTIVE",
    "INACTIVE",
    "INACTIVE",
    "INACTIVE",
    "INACTIVE"
};
const uint8_t USER_BI_INITIAL_VALUES[USER_BI_COUNT] = {
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE
};

const uint32_t USER_BO_INSTANCES[USER_BO_COUNT] = { 1, 2, 3, 4, 5, 6, 7, 8 };
const char *USER_BO_NAMES[USER_BO_COUNT] = {
    "BO1",
    "BO2",
    "BO3",
    "BO4",
    "BO5",
    "BO6",
    "BO7",
    "BO8"
};
const char *USER_BO_DESCRIPTIONS[USER_BO_COUNT] = {
    "Binary Output 1",
    "Binary Output 2",
    "Binary Output 3",
    "Binary Output 4",
    "Binary Output 5",
    "Binary Output 6",
    "Binary Output 7",
    "Binary Output 8"
};
const char *USER_BO_ACTIVE_TEXT[USER_BO_COUNT] = {
    "ON",
    "ON",
    "ON",
    "ON",
    "ON",
    "ON",
    "ON",
    "ON"
};
const char *USER_BO_INACTIVE_TEXT[USER_BO_COUNT] = {
    "OFF",
    "OFF",
    "OFF",
    "OFF",
    "OFF",
    "OFF",
    "OFF",
    "OFF"
};
const uint8_t USER_BO_INITIAL_VALUES[USER_BO_COUNT] = {
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE,
    BINARY_INACTIVE
};
