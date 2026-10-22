/* Verified DevKit wiring: PA1/2 I2C2, PA3 IRQ, PA4 reset, PA5 power. */
#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include <string.h>
#include "board.h"
#include "board57.h"
#include "reader_power.h"
#include "drv_touch.h"
#include "ft5446_packet.h"
static struct rt_device device;
static struct rt_i2c_bus_device *bus;
static struct rt_mutex lock;
static struct rt_semaphore event;
static struct ft_tracker tracker;
static struct touch_message last;
static unsigned initialized,ready,seen,frames,errors,bad,irqs;
static int primary=-1;
static int read_regs(uint8_t reg,void *data,unsigned size){return rt_i2c_mem_read(bus,0x38,reg,8,data,size)==size?0:-RT_EIO;}
static void emit(enum ft_event event_kind,const struct ft_point *point,void *context)
{
    (void)context;
    if(event_kind==FT_DOWN&&primary<0)primary=point->id;
    if(primary!=(int)point->id)return;
    uint16_t x,y;if(ft5446_map(point,0,684,1216,&x,&y))return;
    last=(struct touch_message){.x=x,.y=y,.event=event_kind==FT_DOWN?TOUCH_EVENT_DOWN:
        event_kind==FT_MOVE?TOUCH_EVENT_MOVE:event_kind==FT_UP?TOUCH_EVENT_UP:TOUCH_EVENT_NONE};
    if(device.rx_indicate)device.rx_indicate(&device,1);
    if(event_kind==FT_UP||event_kind==FT_CANCEL)primary=-1;
}
static void off(void)
{
    ready=0;rt_pin_irq_enable(3,PIN_IRQ_DISABLE);ft5446_cancel(&tracker,emit,RT_NULL);
    rt_pin_write(4,0);rt_pin_write(5,0);seen=0;primary=-1;
    /* The touch rail is off. Remove MCU pulls and release the bus/IRQ pads. */
    HAL_PIN_Set_Analog(PAD_PA01,1);HAL_PIN_Set_Analog(PAD_PA02,1);HAL_PIN_Set_Analog(PAD_PA03,1);
}
static int reset(void)
{
    off();rt_thread_mdelay(20);
    HAL_PIN_Set(PAD_PA01,I2C2_SCL,PIN_NOPULL,1);HAL_PIN_Set(PAD_PA02,I2C2_SDA,PIN_NOPULL,1);
    HAL_PIN_Set(PAD_PA03,GPIO_A3,PIN_PULLUP,1);
    rt_pin_write(5,1);rt_thread_mdelay(20);
    rt_pin_write(4,1);rt_thread_mdelay(300);
    uint8_t id=0,fw=0,mode=0,interrupt_mode=1;
    if(read_regs(0xa3,&id,1)||read_regs(0xa6,&fw,1)||read_regs(0,&mode,1)||id!=0x54||(mode&0x70))return -RT_EIO;
    if(rt_i2c_mem_write(bus,0x38,0xa4,8,&interrupt_mode,1)!=1||read_regs(0xa4,&interrupt_mode,1)||interrupt_mode!=1)return -RT_EIO;
    rt_sem_control(&event,RT_IPC_CMD_RESET,RT_NULL);ready=1;rt_pin_irq_enable(3,PIN_IRQ_ENABLE);
    rt_kprintf("FT57_READY id=%02x fw=%02x portrait=684x1216 irq=PA3\n",id,fw);return 0;
}
static rt_size_t read_device(rt_device_t dev,rt_off_t pos,void *buffer,rt_size_t size)
{(void)dev;(void)pos;if(!buffer||!size)return 0;*(struct touch_message *)buffer=last;return 1;}
static rt_err_t control(rt_device_t dev,int command,void *args)
{
    (void)dev;(void)args;int rc=0;reader_power_busy();rt_mutex_take(&lock,RT_WAITING_FOREVER);
    if(command==RTGRAPHIC_CTRL_POWERON)rc=ready?0:reset();
    else if(command==RTGRAPHIC_CTRL_POWEROFF)off();else rc=-RT_ENOSYS;
    rt_mutex_release(&lock);reader_power_idle();return rc;
}
static void irq(void *args){(void)args;++irqs;if(!event.value)rt_sem_release(&event);}
static void worker(void *args)
{
    (void)args;unsigned consecutive=0;
    for(;;){
        int wake=rt_sem_take(&event,tracker.active?rt_tick_from_millisecond(20):RT_WAITING_FOREVER);
        reader_power_busy();rt_mutex_take(&lock,RT_WAITING_FOREVER);
        if(ready&&(wake==RT_EOK||tracker.active)){
            uint8_t raw[FT5446_PACKET_SIZE];struct ft_frame frame;int rc=read_regs(0,raw,sizeof(raw));
            if(rc)++errors;
            else {rc=ft5446_decode(raw,sizeof(raw),&frame);if(rc==FT5446_DECODE_NOT_READY&&!seen)rc=1;else if(rc){++bad;rc=-1;}}
            if(!rc){seen=1;++frames;consecutive=0;ft5446_track(&tracker,&frame,emit,RT_NULL);}
            else if(rc<0&&++consecutive==5)ft5446_cancel(&tracker,emit,RT_NULL);
        }
        rt_mutex_release(&lock);reader_power_idle();
    }
}
int board57_touch_init(void)
{
    if(initialized)return 0;
    rt_mutex_init(&lock,"tp57",RT_IPC_FLAG_PRIO);rt_sem_init(&event,"tp57irq",0,RT_IPC_FLAG_FIFO);
    HAL_PIN_Set(PAD_PA04,GPIO_A4,PIN_NOPULL,1);HAL_PIN_Set(PAD_PA05,GPIO_A5,PIN_NOPULL,1);
    HAL_PIN_Set(PAD_PA03,GPIO_A3,PIN_PULLUP,1);
    rt_pin_mode(4,PIN_MODE_OUTPUT);rt_pin_mode(5,PIN_MODE_OUTPUT);rt_pin_mode(3,PIN_MODE_INPUT);
    struct rt_i2c_configuration config={.mode=0,.addr=0,.timeout=50,.max_hz=100000};
    bus=rt_i2c_bus_device_find("i2c2");
    if(!bus||rt_device_open((rt_device_t)bus,RT_DEVICE_OFLAG_RDWR)||rt_i2c_configure(bus,&config)||
        rt_pin_attach_irq(3,PIN_IRQ_MODE_FALLING,irq,RT_NULL))return -RT_ERROR;
    device.type=RT_Device_Class_Miscellaneous;device.read=read_device;device.control=control;
    if(rt_device_register(&device,"touch",RT_DEVICE_FLAG_RDONLY))return -RT_ERROR;
    int rc=reset();initialized=1;
    rt_thread_t thread=rt_thread_create("touch57",worker,RT_NULL,4096,18,10);
    if(!thread)return -RT_ENOMEM;rt_thread_startup(thread);return rc;
}
void board57_touch_power(int on){if(initialized)control(&device,on?RTGRAPHIC_CTRL_POWERON:RTGRAPHIC_CTRL_POWEROFF,RT_NULL);}
int board57_touch_active(void){return tracker.active!=0;}
static int tp(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"reset")){board57_touch_power(0);board57_touch_power(1);}
    rt_kprintf("TP57_STATUS ready=%u irq=%u frames=%u io_errors=%u bad_reports=%u active=%04x\n",ready,irqs,frames,errors,bad,tracker.active);return 0;
}
MSH_CMD_EXPORT(tp, FT5446 status or reset);
