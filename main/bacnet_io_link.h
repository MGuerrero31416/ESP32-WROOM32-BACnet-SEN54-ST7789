#ifndef BACNET_IO_LINK_H
#define BACNET_IO_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

void bacnet_io_link_init(void);
void bacnet_io_link_task(void *pvParameters);
/* Sync helper: read BACnet BOs and drive physical DOs */
void sync_bacnet_outputs(void);

#ifdef __cplusplus
}
#endif

#endif /* BACNET_IO_LINK_H */
