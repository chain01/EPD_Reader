#pragma once
#include <stdint.h>
#ifndef BOARD527_VERBOSE_LOGS
#define BOARD527_VERBOSE_LOGS 0
#endif
#ifdef __cplusplus
extern "C" {
#endif
int reader_board_frontlight_set(unsigned cold,unsigned warm);
void reader_board_frontlight_get(unsigned *cold,unsigned *warm);
int reader_board_touch_active(void);
void reader_board_touch_power(int on);
void reader_board_display_off(void);
unsigned reader_board_storage_wait_ms(unsigned wait_ms);
#ifdef __cplusplus
}
class Renderer;
void reader_standby_boot();
bool reader_standby_restore();
bool reader_standby_render_saved(Renderer *renderer);
int reader_standby_ram(Renderer *renderer,bool test);
bool reader_standby_ram_take_restored();
bool reader_debug_message(uint32_t message,Renderer *renderer);
#endif
