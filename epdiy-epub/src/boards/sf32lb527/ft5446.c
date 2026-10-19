/* SPDX-License-Identifier: Apache-2.0
 * Verified COM34/36 hardware: I2C1 0x38, PA44 IRQ, GE13 reset, PA32 control.
 * The physical supply does not necessarily switch off with PA32.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include "board.h"
#include "board527.h"
#include "drv_touch.h"
#include "ft5446_packet.h"

static struct rt_device device;
static struct rt_i2c_bus_device *bus;
static struct rt_mutex lock;
static struct rt_semaphore sem;
static struct ft_tracker tracker;
static struct touch_message last;
static volatile unsigned irqs;
static unsigned initialized,ready,frames,errors,bad,aw_gen,seen;
static int primary=-1;

static int read_regs(uint8_t reg,void *data,unsigned n)
{
    return rt_i2c_mem_read(bus,0x38,reg,8,data,n)==n?0:-1;
}
static void publish(uint8_t event,const struct ft_point *p)
{
    uint16_t x,y;
    if(ft5446_map(p,5,1216,684,&x,&y))return;
    last=(struct touch_message){.x=x,.y=y,.event=event};
    if(device.rx_indicate)device.rx_indicate(&device,1);
}
static void emit(enum ft_event event,const struct ft_point *p,void *ctx)
{
    (void)ctx;
    if(event==FT_DOWN && primary<0)primary=p->id;
    if(primary!=(int)p->id)return;
    publish(event==FT_DOWN?TOUCH_EVENT_DOWN:event==FT_MOVE?TOUCH_EVENT_MOVE:
            event==FT_UP?TOUCH_EVENT_UP:TOUCH_EVENT_NONE,p);
    if(event==FT_UP||event==FT_CANCEL)primary=-1;
}
static void disable(void)
{
    ready=0;rt_pin_irq_enable(44,PIN_IRQ_DISABLE);
    ft5446_cancel(&tracker,emit,RT_NULL);
    board_touch_reset(0);rt_pin_write(32,0);
}
static int reset(void)
{
    uint8_t id=0,fw=0,mode=0,gmode=1;
    disable();rt_thread_mdelay(20);
    rt_pin_write(32,1);rt_thread_mdelay(20);
    if(board_touch_reset(1))return -1;
    rt_thread_mdelay(300);
    if(read_regs(0xa3,&id,1)||read_regs(0xa6,&fw,1)||read_regs(0,&mode,1)||id!=0x54||(mode&0x70))return -1;
    if(rt_i2c_mem_write(bus,0x38,0xa4,8,&gmode,1)!=1||read_regs(0xa4,&gmode,1)||gmode!=1)return -1;
    aw_gen=board_expander_generation();seen=0;primary=-1;
    rt_sem_control(&sem,RT_IPC_CMD_RESET,RT_NULL);
    ready=1;rt_pin_irq_enable(44,PIN_IRQ_ENABLE);
    rt_kprintf("FT5446_READY id=%02x fw=%02x physical=1216x684 portrait=684x1216\n",id,fw);
    return 0;
}
static rt_size_t read_device(rt_device_t dev,rt_off_t pos,void *buf,rt_size_t n)
{
    (void)dev;(void)pos;
    if(!buf||!n)return 0;
    *(struct touch_message *)buf=last;return 1;
}
static rt_err_t control(rt_device_t dev,int command,void *arg)
{
    (void)dev;(void)arg;int rc=0;
    rt_mutex_take(&lock,RT_WAITING_FOREVER);
    if(command==RTGRAPHIC_CTRL_POWERON)rc=ready?0:reset();
    else if(command==RTGRAPHIC_CTRL_POWEROFF)disable();
    else rc=-RT_ENOSYS;
    rt_mutex_release(&lock);return rc;
}
static void irq(void *arg)
{
    (void)arg;++irqs;if(!sem.value)rt_sem_release(&sem);
}
static void worker(void *arg)
{
    (void)arg;unsigned consecutive=0;
    while(1) {
        int wake=rt_sem_take(&sem,rt_tick_from_millisecond(20));
        rt_mutex_take(&lock,RT_WAITING_FOREVER);
        if(ready && (aw_gen!=board_expander_generation()||!rt_pin_read(3)))disable();
        if(ready && (wake==RT_EOK||tracker.active)) {
            uint8_t raw[FT5446_PACKET_SIZE];struct ft_frame frame;
            int rc=read_regs(0,raw,sizeof(raw));
            if(rc)++errors;
            else {
                rc=ft5446_decode(raw,sizeof(raw),&frame);
                if(rc==FT5446_DECODE_NOT_READY&&!seen)rc=1;
                else if(rc){++bad;rc=-1;}
            }
            if(!rc){seen=1;++frames;consecutive=0;ft5446_track(&tracker,&frame,emit,RT_NULL);}
            else if(rc<0&&++consecutive==5)ft5446_cancel(&tracker,emit,RT_NULL);
        }
        rt_mutex_release(&lock);
    }
}
static int tp(int argc,char **argv)
{
    (void)argc;(void)argv;
    rt_kprintf("TP_STATUS ready=%u irq=%u frames=%u io_errors=%u bad_reports=%u active=%04x\n",ready,irqs,frames,errors,bad,tracker.active);
    return 0;
}
MSH_CMD_EXPORT(tp, FT5446 status);
int board527_touch_init(void)
{
    struct rt_i2c_configuration cfg={.mode=0,.addr=0,.timeout=50,.max_hz=100000};
    if(initialized)return 0;
    rt_mutex_init(&lock,"tp527",RT_IPC_FLAG_PRIO);rt_sem_init(&sem,"tpirq",0,RT_IPC_FLAG_FIFO);
    HAL_PIN_Set(PAD_PA32,GPIO_A32,PIN_NOPULL,1);rt_pin_mode(32,PIN_MODE_OUTPUT);
    HAL_PIN_Set(PAD_PA44,GPIO_A44,PIN_PULLUP,1);rt_pin_mode(44,PIN_MODE_INPUT);
    bus=rt_i2c_bus_device_find("i2c1");
    if(!bus||rt_device_open((rt_device_t)bus,RT_DEVICE_OFLAG_RDWR)||rt_i2c_configure(bus,&cfg))return -1;
    if(rt_pin_attach_irq(44,PIN_IRQ_MODE_FALLING,irq,RT_NULL))return -1;
    device.type=RT_Device_Class_Miscellaneous;device.read=read_device;device.control=control;
    if(rt_device_register(&device,"touch",RT_DEVICE_FLAG_RDONLY))return -1;
    int rc=reset();initialized=1;
    rt_thread_t t=rt_thread_create("tp527",worker,RT_NULL,4096,18,10);
    if(!t)return -RT_ENOMEM;
    rt_thread_startup(t);return rc;
}
void board527_touch_power(int on){if(initialized)control(&device,on?RTGRAPHIC_CTRL_POWERON:RTGRAPHIC_CTRL_POWEROFF,RT_NULL);}
void board527_touch_lock(void){if(initialized)rt_mutex_take(&lock,RT_WAITING_FOREVER);}
void board527_touch_unlock(void){if(initialized)rt_mutex_release(&lock);}
