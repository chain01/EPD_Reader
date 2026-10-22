#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include <string.h>
#include "board.h"
#include "bf0_pm.h"
#include "reader_power.h"
#include "board57.h"
static unsigned held,entered,exited;
void board57_power_hold(void){if(!held){reader_power_busy();held=1;}}
void board57_power_auto(void){if(held){held=0;reader_power_idle();}}
static void notify(uint8_t event,uint8_t mode,void *data)
{(void)data;if(mode==PM_SLEEP_MODE_DEEP){if(event==RT_PM_ENTER_SLEEP)++entered;else ++exited;}}
int board57_power_init(void)
{
    static const pm_policy_t policy[]={{100,PM_SLEEP_MODE_DEEP}};
    rt_pm_policy_register(sizeof(policy)/sizeof(policy[0]),policy);rt_pm_notify_set(notify,RT_NULL);
    HAL_LPAON_Sleep();
    rt_kprintf("PWR57_READY policy=deep SRAM_PSRAM_retained=1 touch_irq=PA3 gpio_wake=1\n");return 0;
}
static int pwr(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"off"))board57_power_hold();
    if(argc==2&&!strcmp(argv[1],"on"))board57_power_auto();
    rt_kprintf("PWR57_STATUS auto=%u enter=%u exit=%u WER=%08x\n",!held,entered,exited,hwp_hpsys_aon->WER);return 0;
}
MSH_CMD_EXPORT(pwr, Reader deep sleep on/off/status);
