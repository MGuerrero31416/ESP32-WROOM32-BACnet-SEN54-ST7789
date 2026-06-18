#ifndef DISPLAY_H
#define DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the TFT display (TFT_eSPI, ST7789 170x320) */
void display_init(void);

/* Update display with BACnet object values */
void display_update_values(float av1, float av2, float av3, float av4);

/* Update bottom footer with BACnet device instance and current IPv4 address. */
void display_update_footer(unsigned int bacnet_device_id, const char *ip_address);

/* Compatibility hook kept for the main loop; the reference display does not use it. */
void display_set_link_status(bool wifi_connected, bool mstp_connected);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
