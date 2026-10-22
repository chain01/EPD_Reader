#include <rtconfig.h>
#include "reader_board.h"
#if defined(BSP_USING_BOARD_SF32LB57GY_OED_E0470A03)
#include "../sf32lb57/board57.h"
#include "../sf32lb57/standby_clock.h"
#define BOARD_API(name) board57_##name
#define STANDBY_API(name) reader57_standby_##name
extern bool reader57_debug_message(uint32_t,Renderer *);
#define DEBUG_API reader57_debug_message
#else
#include "../sf32lb527/board527.h"
#include "../sf32lb527/standby_clock.h"
#define BOARD_API(name) board527_##name
#define STANDBY_API(name) reader527_standby_##name
extern bool reader527_debug_message(uint32_t,Renderer *);
#define DEBUG_API reader527_debug_message
#endif
int reader_board_frontlight_set(unsigned cold,unsigned warm){return BOARD_API(frontlight_set)(cold,warm);}
void reader_board_frontlight_get(unsigned *cold,unsigned *warm){BOARD_API(frontlight_get)(cold,warm);}
int reader_board_touch_active(void){return BOARD_API(touch_active)();}
void reader_board_touch_power(int on){BOARD_API(touch_power)(on);}
void reader_board_display_off(void){BOARD_API(display_off)();}
unsigned reader_board_storage_wait_ms(unsigned ms){return BOARD_API(storage_wait_ms)(ms);}
void reader_standby_boot(){STANDBY_API(boot)();}
bool reader_standby_restore(){return STANDBY_API(restore)();}
bool reader_standby_render_saved(Renderer *r){return STANDBY_API(render_saved)(r);}
int reader_standby_ram(Renderer *r,bool test){return STANDBY_API(ram)(r,test);}
bool reader_standby_ram_take_restored(){return STANDBY_API(ram_take_restored)();}
bool reader_debug_message(uint32_t msg,Renderer *r){return DEBUG_API(msg,r);}
