/* E0470A03 / SF32LB57 native EPD controller, retained 4bpp framebuffer. */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "board.h"
#include "drv_lcd.h"
#include "mem_section.h"
#include "board57.h"
#include "panel/epd_waveform.h"
#include "panel/epd_power_status.h"
L2_NON_RET_BSS_SECT_BEGIN(reader57_fb)
L2_NON_RET_BSS_SECT(reader57_fb,ALIGN(64) static uint8_t frame[1216*684/2]);
L2_NON_RET_BSS_SECT_END
static rt_device_t lcd;
static unsigned updates_since_clean,period=10;
static int previous_reading_page;
static int known;
uint8_t *board57_framebuffer(void){return frame;}
void set_part_disp_times(int value){period=value>0?(unsigned)value:0;}
static int wait_lcd(void)
{
    uint32_t start=rt_tick_get_millisecond();
    for(;;){bool busy=true;
        if(rt_device_control(lcd,RTGRAPHIC_CTRL_GET_BUSY,&busy))return -RT_ERROR;
        if(!busy)return 0;
        if(rt_tick_get_millisecond()-start>8000)return -RT_ETIMEOUT;
        rt_thread_mdelay(1);
    }
}
static int power_mode(uint8_t idle)
{
    int rc=rt_device_control(lcd,RTGRAPHIC_CTRL_SET_MODE,&idle);
    return rc?rc:wait_lcd();
}
void board57_display_off(void){if(lcd)(void)power_mode(1);}
static int present(const uint8_t *data,int mode)
{
    epd_power_status_t before,after;uint8_t error=0;
    epd_get_power_status(&before);uint32_t start=rt_tick_get_millisecond();
    int rc=power_mode(0);epd_get_power_status(&after);
    if(rc||!after.power_ready){board57_display_off();return -RT_ERROR;}
    epd_wave_set_mode(mode);
    rt_graphix_ops(lcd)->set_window(0,0,1215,683);
    rt_graphix_ops(lcd)->draw_rect((const char *)data,0,0,1215,683);
    rc=wait_lcd();epd_get_power_status(&after);
    if(!rc)rc=rt_device_control(lcd,SF_GRAPHIC_CTRL_GET_DRAW_ERR,&error);
    if(error||after.draws-before.draws!=1||after.errors!=before.errors||after.busy||
        !epd_wave_last_frames()||after.frames-before.frames!=epd_wave_last_frames())rc=-RT_ERROR;
    if(power_mode(1))rc=-RT_ERROR;
    if(rt_pin_read(42)||rt_pin_read(43)||rt_pin_read(44))rc=-RT_ERROR;
    rt_kprintf("EPD57_REFRESH mode=%d frames=%u ms=%u rc=%d\n",mode,after.frames-before.frames,rt_tick_get_millisecond()-start,rc);
    return rc;
}
int board57_display_init(void)
{
    if(lcd)return 0;
    lcd=rt_device_find("lcd");
    uint16_t format=RTGRAPHIC_PIXEL_FORMAT_GRAY4;
    if(!lcd||rt_device_open(lcd,RT_DEVICE_OFLAG_RDWR)||wait_lcd()||
       rt_device_control(lcd,RTGRAPHIC_CTRL_SET_BUF_FORMAT,&format)||wait_lcd())return -RT_ERROR;
    memset(frame,0xff,sizeof(frame));
    int rc=present(frame,3);known=!rc;return rc;
}
int board57_display_present(const uint8_t *data,int reading_page)
{
    if(!known)return -RT_ERROR;
    /* GL16 is reserved for consecutive reading pages. Menus change their
     * layout substantially and must erase the previous controls immediately.
     * Also clean once when returning from a menu to the book. */
    int full=!reading_page||!previous_reading_page||!period||updates_since_clean>=period-1;
    int rc=present(data,full?0:5);
    if(!rc){updates_since_clean=full?0:updates_since_clean+1;previous_reading_page=reading_page;}
    else previous_reading_page=0;
    return rc;
}
int board57_clock_present(const uint8_t *data,int full){return known?present(data,full?0:1):-RT_ERROR;}
int board57_display_prepare(const uint8_t *data,int (*cancel)(void *),void *context)
{
    (void)data;return cancel&&cancel(context)?-RT_EINTR:0;
}
void board57_display_cancel_prepare(void){}
