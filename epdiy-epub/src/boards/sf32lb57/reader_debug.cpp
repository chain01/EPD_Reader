#include "../reader/reader_ui.h"
#include "standby_clock.h"
#include "board57.h"
#include "UIRegionsManager.h"
#include "type.h"
#include "reading_settings.h"
#include "EpubList/EpubReader.h"
#include "epub_mem.h"
#include <cstring>
extern "C" {
#include <finsh.h>
}
extern rt_mq_t ui_queue;
extern Renderer *renderer;
extern AppUIState ui_state;
extern EpubListState epub_list_state;
extern void handleUserInteraction(Renderer *,UIAction,bool);
extern void reader_open_reading_settings(Renderer *);
enum {HOME=0x57010000,LIBRARY,SETTINGS,LIGHT,READING,DFU,DFU_ENTER,CLOCK,CLOCK_TEST,AUTO_OFF,AUTO10,AUTO15,AUTO20};
bool reader57_debug_message(uint32_t message,Renderer *r)
{
    if(message<HOME||message>AUTO20)return false;
    if(message==CLOCK||message==CLOCK_TEST){reader57_standby_ram(r,message==CLOCK_TEST);reader57_standby_ram_take_restored();return true;}
    if(message>=AUTO_OFF&&message<=AUTO20){const unsigned seconds[]={0,10,15,20};bool ok=reading_auto_turn_set(seconds[message-AUTO_OFF]);rt_kprintf("AUTO57 seconds=%u rc=%d\n",reading_auto_turn_seconds(),ok?0:-1);return true;}
    if(message==DFU_ENTER){if(ui_state==USB_DFU_PAGE){reader_dfu_hit=0;handleUserInteraction(r,SELECT,false);}return true;}
    r->cancel_page_preview();
    if(message==HOME)ui_state=MAIN_PAGE;
    if(message==LIBRARY)ui_state=SELECTING_EPUB;
    if(message==SETTINGS)ui_state=SETTINGS_PAGE;
    if(message==LIGHT)ui_state=FRONTLIGHT_PAGE;
    if(message==READING)reader_open_reading_settings(r);
    if(message==DFU)ui_state=USB_DFU_PAGE;
    handleUserInteraction(r,NONE,true);r->flush_display();rt_kprintf("UI57_DONE state=%u\n",(unsigned)ui_state);return true;
}
static int ui(int argc,char **argv)
{
    if(!ui_queue||!renderer)return -RT_ENOSYS;
    if(argc==2&&!strcmp(argv[1],"status")){
        rt_kprintf("UI57_STATUS state=%u page=%dx%d auto=%u free_psram=%u\n",(unsigned)ui_state,renderer->get_page_width(),renderer->get_page_height(),reading_auto_turn_seconds(),(unsigned)epub_mem_available_size());
        if(epub_list_state.num_epubs&&epub_list_state.selected_item<epub_list_state.num_epubs){const auto &book=epub_list_state.epub_list[epub_list_state.selected_item];rt_kprintf("UI57_BOOK section=%u page=%u pages=%u\n",book.current_section,book.current_page,book.pages_in_current_section);}
        return 0;
    }
    if(argc==2){
        const char *names[]={"home","library","settings","light","reading","dfu","dfu-enter","clock-ram","clock-ram-test","auto-off","auto-10","auto-15","auto-20"};
        for(unsigned n=0;n<sizeof(names)/sizeof(names[0]);++n)if(!strcmp(argv[1],names[n])){uint32_t message=HOME+n;return rt_mq_send(ui_queue,&message,sizeof(message));}
        const char *actions[]={"prev","next","select","menu"};const UIAction values[]={UP,DOWN,SELECT,UPGLIDE};
        for(unsigned n=0;n<4;++n)if(!strcmp(argv[1],actions[n])){uint32_t message=values[n];return rt_mq_send(ui_queue,&message,sizeof(message));}
    }
    rt_kprintf("ui status/home/library/settings/light/reading/dfu/dfu-enter/clock-ram/clock-ram-test/auto-off/auto-10/auto-15/auto-20/prev/next/select/menu\n");return -RT_EINVAL;
}
MSH_CMD_EXPORT(ui, Reader UI diagnostics);
