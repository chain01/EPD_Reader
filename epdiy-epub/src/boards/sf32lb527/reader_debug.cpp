#include "reader_ui.h"
#include "UIRegionsManager.h"
#include "type.h"
#include "reading_settings.h"
#include <cstring>
extern "C" {
#include <finsh.h>
#include "board527.h"
}
extern rt_mq_t ui_queue;
extern Renderer *renderer;
extern AppUIState ui_state;
extern void handleUserInteraction(Renderer *,UIAction,bool);
extern void back_to_main_page();
enum {DEBUG_HOME=0x52710000,DEBUG_LIBRARY,DEBUG_SETTINGS,DEBUG_LIGHT,DEBUG_READING,DEBUG_DUMP};
static void dump_frame(void)
{
    const uint8_t *frame=board527_framebuffer();const unsigned size=1216*684/2;
    const char hex[]="0123456789abcdef";uint32_t hash=2166136261u;
    rt_kprintf("FB_BEGIN bytes=%u width=1216 height=684 rotation=3\n",size);
    for(unsigned at=0;at<size;at+=32) {
        char line[65];unsigned n=size-at;if(n>32)n=32;
        for(unsigned i=0;i<n;++i) {
            uint8_t v=frame[at+i];line[i*2]=hex[v>>4];line[i*2+1]=hex[v&15];hash=(hash^v)*16777619u;
        }
        line[n*2]=0;rt_kprintf("FB:%06x %s\n",at,line);
    }
    rt_kprintf("FB_END fnv=%08x\n",hash);
}
bool reader527_debug_message(uint32_t msg,Renderer *r)
{
    if(msg<DEBUG_HOME||msg>DEBUG_DUMP)return false;
    if(msg==DEBUG_DUMP){dump_frame();return true;}
    if(msg==DEBUG_HOME){ui_state=MAIN_PAGE;}
    else if(msg==DEBUG_LIBRARY)ui_state=SELECTING_EPUB;
    else if(msg==DEBUG_SETTINGS)ui_state=SETTINGS_PAGE;
    else if(msg==DEBUG_LIGHT)ui_state=FRONTLIGHT_PAGE;
    else if(msg==DEBUG_READING){ui_state=READING_SETTINGS;reading_settings_draw(r);}
    handleUserInteraction(r,NONE,true);r->flush_display();
    rt_kprintf("UI_DEBUG_DONE state=%u\n",(unsigned)ui_state);return true;
}
static int ui(int argc,char **argv)
{
    if(!ui_queue||!renderer)return -RT_ENOSYS;
    if(argc==2&&!strcmp(argv[1],"status")) {
        rt_kprintf("UI_STATUS state=%u width=684 height=1216 page=%d,%d margins=%d,%d\n",
            (unsigned)ui_state,renderer->get_page_width(),renderer->get_page_height(),renderer->get_margin_left(),renderer->get_margin_top());
        for(unsigned n=0;n<MAX_AREAS;++n) {
            AreaRect &a=g_area_array[n];
            if(a.end_x>a.start_x&&a.end_y>a.start_y)
                rt_kprintf("UI_AREA %u %d,%d..%d,%d\n",n,a.start_x,a.start_y,a.end_x,a.end_y);
        }
        return 0;
    }
    if(argc==2) {
        const char *names[]={"home","library","settings","light","reading","dump"};
        for(unsigned n=0;n<6;++n)if(!strcmp(argv[1],names[n])) {
            uint32_t msg=DEBUG_HOME+n;return rt_mq_send(ui_queue,&msg,sizeof(msg));
        }
        const char *actions[]={"prev","next","select","menu"};
        const UIAction values[]={UP,DOWN,SELECT,UPGLIDE};
        for(unsigned n=0;n<4;++n)if(!strcmp(argv[1],actions[n])) {
            uint32_t msg=values[n];return rt_mq_send(ui_queue,&msg,sizeof(msg));
        }
    }
    rt_kprintf("ui home/library/settings/light/reading/dump/status/prev/next/select/menu\n");return -RT_EINVAL;
}
MSH_CMD_EXPORT(ui, Reader UI diagnostics);
