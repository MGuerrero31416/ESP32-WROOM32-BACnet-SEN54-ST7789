#ifndef WAVESHARE_IO_H
#define WAVESHARE_IO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Clear constants for GPIO assignments */
#define WAVESHARE_DI_GPIO0 4
#define WAVESHARE_DI_GPIO1 5
#define WAVESHARE_DI_GPIO2 6
#define WAVESHARE_DI_GPIO3 7
#define WAVESHARE_DI_GPIO4 8
#define WAVESHARE_DI_GPIO5 9
#define WAVESHARE_DI_GPIO6 10
#define WAVESHARE_DI_GPIO7 11

#define WAVESHARE_I2C_SDA_GPIO 42
#define WAVESHARE_I2C_SCL_GPIO 41

/* TCA9554 I2C default address */
#ifndef WAVESHARE_TCA9554_ADDR
#define WAVESHARE_TCA9554_ADDR 0x20
#endif

/** Initialize Waveshare I/O module.
 * - Initializes I2C and TCA9554 expander
 * - Configures all expander pins as outputs (driven low)
 * - Configures DI GPIOs as inputs
 */
void waveshare_io_init(void);

/** Read a digital input channel (0..7) -> returns true if high */
bool waveshare_read_di(uint8_t channel);

/** Write a digital output channel (0..7) to state */
void waveshare_write_do(uint8_t channel, bool state);

/** Read back cached/observed DO state (0..7) */
bool waveshare_read_do(uint8_t channel);

/* Test helper: sequentially activate DO0..DO7 for 1 second each. Bypasses BACnet. */
void test_outputs(void);

#ifdef __cplusplus
}
#endif

#endif /* WAVESHARE_IO_H */
