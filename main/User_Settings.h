#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

/* WiFi settings */
extern const bool USER_ENABLE_BACNET_IP;
extern const char USER_WIFI_SSID[];
extern const char USER_WIFI_PASS[];
extern const bool USER_WIFI_USE_STATIC_IP;
extern const char USER_WIFI_STATIC_IP_ADDR[];
extern const char USER_WIFI_STATIC_IP_GATEWAY[];
extern const char USER_WIFI_STATIC_IP_NETMASK[];

/* BACnet device settings */
extern const char USER_BACNET_DEVICE_NAME[];
extern const uint32_t USER_BACNET_DEVICE_INSTANCE;
extern const int USER_OVERRIDE_NVS_ON_FLASH;

/* BBMD foreign device registration */
extern const uint8_t USER_BBMD_IP_OCTET_1;
extern const uint8_t USER_BBMD_IP_OCTET_2;
extern const uint8_t USER_BBMD_IP_OCTET_3;
extern const uint8_t USER_BBMD_IP_OCTET_4;
extern const uint16_t USER_BBMD_PORT;
extern const uint16_t USER_BBMD_TTL_SECONDS;

/* BACnet MS/TP settings */
extern const bool USER_ENABLE_BACNET_MSTP;
extern const uint8_t USER_MSTP_MAC_ADDRESS;
extern const uint8_t USER_MSTP_MAX_INFO_FRAMES;
extern const uint8_t USER_MSTP_MAX_MASTER;
extern const uint32_t USER_MSTP_BAUD_RATE;

/* BACnet object defaults */
#define USER_AV_COUNT 7
#define USER_BV_COUNT 4
#define USER_AI_COUNT 4
#define USER_BI_COUNT 8
#define USER_BO_COUNT 8

extern const uint32_t USER_AV_INSTANCES[USER_AV_COUNT];
extern const char *USER_AV_NAMES[USER_AV_COUNT];
extern const char *USER_AV_DESCRIPTIONS[USER_AV_COUNT];
extern const uint16_t USER_AV_UNITS[USER_AV_COUNT];
extern const float USER_AV_INITIAL_VALUES[USER_AV_COUNT];
extern const float USER_AV_COV_INCREMENTS[USER_AV_COUNT];

extern const uint32_t USER_BV_INSTANCES[USER_BV_COUNT];
extern const char *USER_BV_NAMES[USER_BV_COUNT];
extern const char *USER_BV_DESCRIPTIONS[USER_BV_COUNT];
extern const char *USER_BV_ACTIVE_TEXT[USER_BV_COUNT];
extern const char *USER_BV_INACTIVE_TEXT[USER_BV_COUNT];
extern const uint8_t USER_BV_INITIAL_VALUES[USER_BV_COUNT];

extern const uint32_t USER_AI_INSTANCES[USER_AI_COUNT];
extern const char *USER_AI_NAMES[USER_AI_COUNT];
extern const char *USER_AI_DESCRIPTIONS[USER_AI_COUNT];
extern const uint16_t USER_AI_UNITS[USER_AI_COUNT];
extern const float USER_AI_INITIAL_VALUES[USER_AI_COUNT];
extern const float USER_AI_COV_INCREMENTS[USER_AI_COUNT];

extern const uint32_t USER_BI_INSTANCES[USER_BI_COUNT];
extern const char *USER_BI_NAMES[USER_BI_COUNT];
extern const char *USER_BI_DESCRIPTIONS[USER_BI_COUNT];
extern const char *USER_BI_ACTIVE_TEXT[USER_BI_COUNT];
extern const char *USER_BI_INACTIVE_TEXT[USER_BI_COUNT];
extern const uint8_t USER_BI_INITIAL_VALUES[USER_BI_COUNT];

extern const uint32_t USER_BO_INSTANCES[USER_BO_COUNT];
extern const char *USER_BO_NAMES[USER_BO_COUNT];
extern const char *USER_BO_DESCRIPTIONS[USER_BO_COUNT];
extern const char *USER_BO_ACTIVE_TEXT[USER_BO_COUNT];
extern const char *USER_BO_INACTIVE_TEXT[USER_BO_COUNT];
extern const uint8_t USER_BO_INITIAL_VALUES[USER_BO_COUNT];

#endif /* USER_SETTINGS_H */
