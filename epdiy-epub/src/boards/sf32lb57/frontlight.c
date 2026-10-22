/* AW99703: shared 11-bit brightness, CH2 cold and CH3 warm, PA53 HWEN.
 * Preserve the measured 4.8mA/string limit and guarded low-current startup. */
#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include <string.h>
#include <stdlib.h>
#include "board.h"
#include "board57.h"
#include "reader_power.h"
static struct rt_i2c_bus_device *bus;
static struct rt_mutex lock;
static struct rt_semaphore event;
static unsigned started,ready,cold_level,warm_level,mask,errors;
static int read_reg(uint8_t reg,uint8_t *value){return rt_i2c_mem_read(bus,0x36,reg,8,value,1)==1?0:-RT_EIO;}
static int write_check(uint8_t reg,uint8_t value)
{
    uint8_t got=0;if(rt_i2c_mem_write(bus,0x36,reg,8,&value,1)!=1||read_reg(reg,&got)||got!=value){
        rt_kprintf("FL57_REG_FAIL reg=%02x want=%02x got=%02x\n",reg,value,got);return -RT_EIO;}return 0;
}
static void hard_off(void){rt_pin_write(53,0);ready=cold_level=warm_level=mask=0;}
static int fail_off(void){++errors;hard_off();rt_kprintf("FL57_FAULT HWEN=0 errors=%u\n",errors);return -RT_EIO;}
static int faults(void)
{
    uint8_t f1=0,f2=0;if(read_reg(0x0e,&f1)||read_reg(0x0f,&f2)||f1||(f2&0xe0))return fail_off();return 0;
}
static int prepare(void)
{
    if(ready)return 0;
    uint8_t id=0,f1=0,f2=0;
    if(!bus){bus=rt_i2c_bus_device_find("i2c1");if(!bus||rt_device_open((rt_device_t)bus,RT_DEVICE_OFLAG_RDWR))return fail_off();}
    hard_off();rt_thread_mdelay(2);rt_pin_write(53,1);rt_thread_mdelay(2);
    if(read_reg(0,&id)||id!=3||read_reg(0x0e,&f1)||read_reg(0x0f,&f2))return fail_off();
    rt_thread_mdelay(100);
    if(read_reg(0x0e,&f1)||read_reg(0x0f,&f2)||f1||(f2&0xa0))return fail_off();
    if(write_check(2,0x14)||write_check(3,6)||write_check(4,0x2c)||write_check(5,0x40)||
       write_check(6,0)||write_check(7,0)||write_check(9,0x44)||write_check(10,0))return fail_off();
    ready=1;mask=6;return 0;
}
static int level(unsigned value)
{
    unsigned code=(value*2047u+50)/100;
    if(write_check(6,code&7)||write_check(7,code>>3)||write_check(2,0x15))return fail_off();
    rt_thread_mdelay(100);return faults();
}
int board57_frontlight_set(unsigned cold,unsigned warm)
{
    if(cold>100||warm>100||(cold&&warm&&cold!=warm))return -RT_EINVAL;
    if(!started)return -RT_ENOSYS;
    reader_power_busy();rt_mutex_take(&lock,RT_WAITING_FOREVER);int rc=0;
    if(!cold&&!warm){if(ready){rc=write_check(2,0x14);rt_thread_mdelay(10);}hard_off();}
    else if(!(rc=prepare())){
        unsigned channels=(cold?2:0)|(warm?4:0),brightness=cold?cold:warm;
        int starting=!(cold_level||warm_level)||mask!=channels;
        if(mask!=channels){rc=write_check(2,0x14);rt_thread_mdelay(10);if(!rc)rc=write_check(3,channels);}
        if(!rc&&starting)rc=level(brightness>5?5:brightness);
        if(!rc)rc=level(brightness);
        if(!rc){cold_level=cold;warm_level=warm;mask=channels;if(!event.value)rt_sem_release(&event);}
        else hard_off();
    }
    rt_mutex_release(&lock);reader_power_idle();return rc;
}
void board57_frontlight_get(unsigned *cold,unsigned *warm)
{
    if(!started){*cold=*warm=0;return;}
    rt_mutex_take(&lock,RT_WAITING_FOREVER);*cold=cold_level;*warm=warm_level;rt_mutex_release(&lock);
}
static void monitor(void *args)
{
    (void)args;
    for(;;){rt_sem_take(&event,(cold_level||warm_level)?rt_tick_from_millisecond(2000):RT_WAITING_FOREVER);
        reader_power_busy();rt_mutex_take(&lock,RT_WAITING_FOREVER);if(ready&&(cold_level||warm_level))faults();
        rt_mutex_release(&lock);reader_power_idle();}
}
int board57_frontlight_init(void)
{
    if(started)return 0;
    HAL_PIN_Set(PAD_PA53,GPIO_A53,PIN_NOPULL,1);rt_pin_mode(53,PIN_MODE_OUTPUT);hard_off();
    rt_mutex_init(&lock,"fl57",RT_IPC_FLAG_PRIO);rt_sem_init(&event,"fl57evt",0,RT_IPC_FLAG_FIFO);started=1;
    rt_thread_t thread=rt_thread_create("fl57",monitor,RT_NULL,2048,22,10);
    if(!thread)return -RT_ENOMEM;rt_thread_startup(thread);return 0;
}
static int fl(int argc,char **argv)
{
    int rc=0;
    if(argc==2&&!strcmp(argv[1],"off"))rc=board57_frontlight_set(0,0);
    else if(argc==3){unsigned percent=atoi(argv[2]);
        rc=!strcmp(argv[1],"cold")?board57_frontlight_set(percent,0):!strcmp(argv[1],"warm")?
            board57_frontlight_set(0,percent):!strcmp(argv[1],"both")?board57_frontlight_set(percent,percent):-RT_EINVAL;}
    unsigned cold,warm;board57_frontlight_get(&cold,&warm);
    rt_kprintf("FL57_STATUS cold=%u warm=%u shared_brightness=1 HWEN=%d errors=%u rc=%d\n",cold,warm,rt_pin_read(53),errors,rc);return rc;
}
MSH_CMD_EXPORT(fl, AW99703 status or off or cold/warm/both percent);
