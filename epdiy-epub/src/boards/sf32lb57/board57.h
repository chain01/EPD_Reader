#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int board57_init(void);
int board57_display_init(void);
int board57_display_present(const uint8_t *frame,int reading_page);
int board57_clock_present(const uint8_t *frame,int full);
int board57_display_prepare(const uint8_t *frame,int (*cancel)(void *),void *context);
void board57_display_cancel_prepare(void);
uint8_t *board57_framebuffer(void);
void board57_display_off(void);
int board57_touch_init(void);
void board57_touch_power(int on);
int board57_touch_active(void);
int board57_frontlight_init(void);
int board57_frontlight_set(unsigned cold,unsigned warm);
void board57_frontlight_get(unsigned *cold,unsigned *warm);
int board57_storage_mount(void);
unsigned board57_storage_wait_ms(unsigned wait_ms);
int board57_power_init(void);
void board57_power_auto(void);
void board57_power_hold(void);
int board57_unused_off(void);
#ifdef __cplusplus
}
#endif
