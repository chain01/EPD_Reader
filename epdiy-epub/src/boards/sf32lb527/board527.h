/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EPD_READER_BOARD527_H
#define EPD_READER_BOARD527_H
#include <stdint.h>
#include "epd_tps.h"
#ifdef __cplusplus
extern "C" {
#endif
int board527_init(void);
int board527_display_init(void);
int board527_display_present(const uint8_t *gray4);
uint8_t *board527_framebuffer(void);
void board527_display_off(void);
int board527_touch_init(void);
void board527_touch_power(int on);
void board527_touch_lock(void);
void board527_touch_unlock(void);
int board527_storage_mount(void);
int board527_frontlight_set(unsigned cold,unsigned warm);
void board527_frontlight_get(unsigned *cold,unsigned *warm);
#ifdef __cplusplus
}
#endif
#endif
