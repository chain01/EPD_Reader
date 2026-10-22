#include "../SF32Paper.h"
#include "../SF32PaperRenderer.h"
#include "../controls/SF32_TouchControls.h"
#include <hourglass.h>
extern "C" {
#include "board57.h"
#include "font_ft.h"
extern const EpdFont regular_font;
}
class Reader57Buttons:public ButtonControls {
public:
    bool did_wake_from_deep_sleep() override{return false;}
    UIAction get_deep_sleep_action() override{return NONE;}
};
void SF32Paper::power_up(){if(board57_init())rt_kprintf("BOARD57_INIT_ERROR\n");if(epd_font_ft_init_builtin(32))rt_kprintf("FONT57_INIT_ERROR\n");}
void SF32Paper::start_filesystem(){(void)board57_storage_mount();}
void SF32Paper::stop_filesystem(){}
void SF32Paper::sleep_filesystem(){}
void SF32Paper::wakeup_filesystem(){}
void SF32Paper::prepare_to_sleep(){board57_frontlight_set(0,0);board57_touch_power(0);board57_display_off();}
Renderer *SF32Paper::get_renderer(){return new SF32PaperRenderer(&regular_font,&regular_font,&regular_font,&regular_font,hourglass_data,hourglass_width,hourglass_height);}
ButtonControls *SF32Paper::get_button_controls(rt_mq_t queue){(void)queue;return new Reader57Buttons;}
TouchControls *SF32Paper::get_touch_controls(Renderer *r,rt_mq_t queue){return new SF32_TouchControls(r,[queue](UIAction a){rt_mq_send(queue,&a,sizeof(a));});}
