#include <rtthread.h>
#include <rtdevice.h>
#include <dfs_fs.h>
#include "board.h"
#include "board57.h"
extern void BSP_LowPowerInit(void);
extern int epd_unused_sensor_off(void);
int board57_unused_off(void)
{
    BSP_GPIO_Set(54,0,1);BSP_GPIO_Set(57,0,1);
    return epd_unused_sensor_off();
}
int board57_init(void)
{
    int rc=0;
    (void)board57_unused_off();
    if(board57_frontlight_init())rc=-RT_ERROR;
    if(board57_display_init())rc=-RT_ERROR;
    if(board57_touch_init())rc=-RT_ERROR;
    BSP_LowPowerInit();
    if(board57_power_init())rc=-RT_ERROR;
    rt_kprintf("BOARD57_READY chip=SF32LB57GY display=1216x684 portrait=684x1216 rc=%d\n",rc);
    return rc;
}
int board57_storage_mount(void)
{
    static int mounted;
    if(mounted)return 0;
    const char *name=0;
    for(unsigned n=0;n<100;++n){
        if(rt_device_find("sd1")){name="sd1";break;}
        if(rt_device_find("sd0")){name="sd0";break;}
        rt_thread_mdelay(30);
    }
    if(!name||dfs_mount(name,"/","elm",0,RT_NULL)){
        rt_kprintf("TF57_UNAVAILABLE detect=%d; existing card contents retained\n",rt_pin_read(51));return -RT_ERROR;
    }
    mounted=1;rt_kprintf("TF57_READY device=%s bus=SDMMC2 width=4 root=/\n",name);return 0;
}
unsigned board57_storage_wait_ms(unsigned wait_ms){return wait_ms;}
