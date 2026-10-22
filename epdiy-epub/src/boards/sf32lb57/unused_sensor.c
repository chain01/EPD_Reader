/* SPDX-License-Identifier: Apache-2.0 */
#include <rtthread.h>
#include <rtdevice.h>
#include "board.h"
#include "bf0_hal.h"

static int reg_read(struct rt_i2c_bus_device *bus, uint8_t reg, uint8_t *v)
{
    return rt_i2c_mem_read(bus, 0x19, reg, 8, v, 1)==1 ? 0 : -1;
}

int epd_unused_sensor_off(void)
{
    struct rt_i2c_bus_device *bus=(struct rt_i2c_bus_device *)rt_device_find("i2c1");
    static const uint8_t regs[]={0x20,0x1f,0x21,0x22,0x23,0x24,0x25,0x2e};
    unsigned i;
    uint8_t id=0, zero=0, v=0xff;
    if (!bus || reg_read(bus,0x0f,&id) || id!=0x11) {
        rt_kprintf("LP_SENSOR unavailable id=%02x\n",id);
        return -RT_ERROR;
    }
    /* SC7A20HTR: ODR/axes off, high-performance mode off, FIFO bypass,
     * interrupts off. The rail is shared and cannot be cut in software. */
    for(i=0;i<sizeof(regs);++i) {
        if(rt_i2c_mem_write(bus,0x19,regs[i],8,&zero,1)!=1 ||
           reg_read(bus,regs[i],&v) || v!=0) return -RT_ERROR;
    }
    rt_kprintf("LP_SENSOR id=%02x odr=0 fifo=0 interrupts=0 verified\n",id);
    return RT_EOK;
}

int epd_idle_pins_check(void)
{
    static const uint8_t lows[]={4,5,8,9,10,14,24,25,26,27,28,29,30,31,32,33,
                                 37,38,39,40,41,42,43,44,47,53,54,55,57};
    unsigned i;
    for(i=0;i<sizeof(lows);++i)
        if(rt_pin_read(lows[i])) return -RT_ERROR;
    /* The common I2C bus and always-powered TF CMD must not sink pull-up current. */
    if(!rt_pin_read(20) || !rt_pin_read(21) || !rt_pin_read(48)) return -RT_ERROR;
    return RT_EOK;
}
