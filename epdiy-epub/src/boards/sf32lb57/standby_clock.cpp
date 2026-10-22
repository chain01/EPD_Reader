#include "standby_clock.h"
#include "board57.h"
#include "UIRegionsManager.h"
#include "reading_settings.h"
#include "reader_power.h"
#include "EpubList/EpubReader.h"
#include "epub_mem.h"
extern "C" {
#include "board.h"
#include "bf0_pm.h"
#include "font_manager.h"
#include "font_ft.h"
extern RTC_HandleTypeDef RTC_Handler;
}
extern EpubReader *reader;
extern rt_mq_t ui_queue;
static struct rt_semaphore event;
static bool event_ready,restored;
static volatile bool active,key_seen,rtc_seen;
static RTC_cb saved_callback;
static void key_irq(void *){key_seen=true;if(active)rt_sem_release(&event);}
static void rtc_callback(int reason)
{
    if(active&&reason==RTC_CBK_WAKEUP){rtc_seen=true;rt_sem_release(&event);}
    else if(saved_callback)saved_callback(reason);
}
static void stop_rtc()
{
    HAL_RTC_DeactivateWakeUpTimer(&RTC_Handler);
    HAL_PMU_DisableRtcWakeup();
    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&RTC_Handler,RTC_FLAG_WUTF);
}
static bool read_time(unsigned &minute,unsigned &second)
{
    RTC_TimeTypeDef time={};RTC_DateTypeDef date={};
    if(HAL_RTC_GetTime(&RTC_Handler,&time,RTC_FORMAT_BIN)!=HAL_OK||HAL_RTC_GetDate(&RTC_Handler,&date,RTC_FORMAT_BIN)!=HAL_OK)return false;
    minute=time.Hours*60+time.Minutes;second=time.Seconds;return minute<1440&&second<60;
}
static void center(Renderer *r,int y,int size,const char *text)
{epd_font_ft_set_size(size);r->draw_text((684-r->get_text_width(text))/2,y,text);}
static void draw_time(Renderer *r,unsigned minute)
{
    r->fill_rect(0,420,684,280,255);char text[8];rt_snprintf(text,sizeof(text),"%02u:%02u",minute/60,minute%60);
    center(r,430,178,text);uint8_t *fb=board57_framebuffer();
    for(unsigned n=0;n<1216*684/2;++n)fb[n]=((fb[n]&15)>=8?15:0)|((fb[n]>>4)>=8?240:0);
}
static void draw_clock(Renderer *r,unsigned minute)
{
    r->set_margin_top(0);r->set_margin_bottom(0);r->set_margin_left(0);r->set_margin_right(0);
    r->clear_screen();epd_font_ft_set_bold(0);epd_font_ft_set_line_spacing(1.0f);
    center(r,90,28,"S I F L I   R E A D E R");r->fill_rect(294,182,96,3,0);
    center(r,242,40,"休眠待机");r->fill_rect(62,706,560,2,0);center(r,754,28,"让时间慢下来");
    center(r,1022,32,"轻按电源键唤醒");center(r,1084,22,"触摸与前光已关闭");draw_time(r,minute);
}
void reader57_standby_boot(){}
bool reader57_standby_restore(){return false;}
bool reader57_standby_render_saved(Renderer *){return false;}
int reader57_standby_ram(Renderer *r,bool test)
{
    restored=false;
    uint8_t *saved=(uint8_t *)epub_mem_malloc(1216*684/2);if(!saved)return -RT_ENOMEM;
    int rc=0;
    int top=r->get_margin_top(),left=r->get_margin_left();
    int bottom=1216-top-r->get_page_height(),right=684-left-r->get_page_width();
    AreaRect areas[MAX_AREAS];memcpy(areas,g_area_array,sizeof(areas));int area_count=g_area_count;
    unsigned cold=0,warm=0,minute=0,second=0,shown=0,partials=0,updates=0;
    uint32_t saved_wer=hwp_hpsys_aon->WER,saved_pmuc_wer=hwp_pmuc->WER;int wake_pin=HAL_HPAON_QueryWakeupPin(hwp_gpio1,34);
    if(reader)reader->pause_background();r->cancel_page_preview();epd_font_ft_preheat_stop();
    memcpy(saved,board57_framebuffer(),1216*684/2);board57_frontlight_get(&cold,&warm);
    if(font_manager_select(0,28)||board57_frontlight_set(0,0)||!read_time(minute,second)||wake_pin<0){rc=-RT_ERROR;goto done;}
    board57_touch_power(0);(void)board57_unused_off();clear_areas();draw_clock(r,minute);shown=minute;
    if(board57_clock_present(board57_framebuffer(),1)){rc=-RT_EIO;goto done;}
    if(!event_ready){if(rt_sem_init(&event,"clk57",0,RT_IPC_FLAG_FIFO)){rc=-RT_ERROR;goto done;}event_ready=true;}
    HAL_PIN_Set(PAD_PA34,GPIO_A34,PIN_PULLDOWN,1);rt_pin_mode(34,PIN_MODE_INPUT);
    key_seen=rt_pin_read(34)!=0;
    if(rt_pin_attach_irq(34,PIN_IRQ_MODE_RISING,key_irq,nullptr)||rt_pin_irq_enable(34,PIN_IRQ_ENABLE)||
        pm_enable_pin_wakeup(wake_pin,AON_PIN_MODE_POS_EDGE)||pm_enable_rtc_wakeup()){rc=-RT_ERROR;goto done;}
    saved_callback=RTC_Handler.callback;HAL_RTC_RegCallback(&RTC_Handler,rtc_callback);active=true;
    /* SF57 AON pins are routed through PMUC, unlike the SF52 WER layout. */
    hwp_pmuc->WER=(PMUC_WER_PA33<<wake_pin)|PMUC_WER_RTC;
    hwp_hpsys_aon->WER=HPSYS_AON_WER_RTC;
    rt_kprintf("CLOCK57_START test=%u key=PA34 wake_pin=%d retained=SRAM+PSRAM\n",test,wake_pin);
    while(!rc&&!key_seen&&(!test||updates<11)){
        if(!read_time(minute,second)){rc=-RT_EIO;break;}
        unsigned seconds=test?5:60-second;rtc_seen=false;rt_sem_control(&event,RT_IPC_CMD_RESET,nullptr);stop_rtc();
        if(HAL_RTC_SetWakeUpTimer(&RTC_Handler,seconds-1,RTC_WAKEUP_SEC)!=HAL_OK||pm_enable_rtc_wakeup()){rc=-RT_EIO;break;}
        if(!key_seen){reader_power_idle();int wait=rt_sem_take(&event,rt_tick_from_millisecond((seconds+3)*1000));reader_power_busy();if(wait){rc=-RT_ETIMEOUT;break;}}
        stop_rtc();if(key_seen)break;
        if(!rtc_seen||!read_time(minute,second)){rc=-RT_EIO;break;}
        if(test)minute=(shown+1)%1440;
        bool full=partials>=10;draw_time(r,minute);uint32_t start=rt_tick_get_millisecond();
        rc=board57_clock_present(board57_framebuffer(),full);if(rc)break;
        shown=minute;partials=full?0:partials+1;++updates;
        rt_kprintf("CLOCK57_TICK kind=%s count=%u ms=%u\n",full?"FULL":"PARTIAL",updates,rt_tick_get_millisecond()-start);
    }
done:
    stop_rtc();if(active){active=false;HAL_RTC_RegCallback(&RTC_Handler,saved_callback);}
    rt_pin_irq_enable(34,PIN_IRQ_DISABLE);rt_pin_detach_irq(34);if(wake_pin>=0)pm_disable_pin_wakeup(wake_pin);
    pm_disable_rtc_wakeup();hwp_hpsys_aon->WER=saved_wer;hwp_pmuc->WER=saved_pmuc_wer;
    reading_settings_load(r);r->set_margin_top(top);r->set_margin_bottom(bottom);r->set_margin_left(left);r->set_margin_right(right);
    memcpy(board57_framebuffer(),saved,1216*684/2);epub_mem_free(saved);
    if(board57_clock_present(board57_framebuffer(),1))rc=-RT_EIO;
    board57_touch_power(1);if(board57_frontlight_set(cold,warm))rc=-RT_EIO;
    memcpy(g_area_array,areas,sizeof(areas));g_area_count=area_count;
    if(reader)reader->resume_background();
    if(ui_queue){uint32_t message;while(rt_mq_recv(ui_queue,&message,sizeof(message),0)==RT_EOK){}}
    restored=rc==0;rt_kprintf("CLOCK57_DONE rc=%d updates=%u key=%u\n",rc,updates,key_seen);return rc;
}
bool reader57_standby_ram_take_restored(){bool value=restored;restored=false;return value;}
