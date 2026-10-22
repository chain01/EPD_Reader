/* SPDX-FileCopyrightText: 2026 SiFli Technologies(Nanjing) Co., Ltd
 * SPDX-License-Identifier: Apache-2.0
 * TPS65185 registers: TI SLVSAQ8G, section 8.6.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "epd_tps.h"
#ifndef EPD_POWER_DIAGNOSTICS
#define rt_kprintf(...) ((void)0)
#endif

#define TPS_ADDR 0x68
#define REG_VADJ 0x02
#define REG_VCOM1 0x03
#define REG_VCOM2 0x04
#define REG_PG 0x0f
#define REG_REVID 0x10
#define PG_ALL_RAILS 0xfa

static struct rt_i2c_bus_device *bus;
static rt_bool_t configured;
static rt_bool_t powered;
static rt_uint32_t power_cycles;
static rt_uint32_t power_errors;
static uint16_t saved_vcom_mv;

static void pgood_pull(int enable)
{
    HAL_PIN_Set(PAD_PA00 + EPD_TPS_GOOD_PIN, GPIO_A0 + EPD_TPS_GOOD_PIN,
                enable ? PIN_PULLUP : PIN_NOPULL, 1);
    HAL_PIN_SetMode(PAD_PA00 + EPD_TPS_GOOD_PIN, 1,
                   enable ? PIN_DIGITAL_IO_PULLUP : PIN_ANALOG_INPUT);
}

static rt_err_t read_reg(rt_uint8_t reg, rt_uint8_t *value)
{
    struct rt_i2c_msg msg[2] = {
        {.addr=TPS_ADDR, .flags=RT_I2C_WR, .len=1, .buf=&reg},
        {.addr=TPS_ADDR, .flags=RT_I2C_RD, .len=1, .buf=value},
    };
    return bus && rt_i2c_transfer(bus, msg, 2) == 2 ? RT_EOK : -RT_ERROR;
}

static rt_err_t write_checked(rt_uint8_t reg, rt_uint8_t value)
{
    rt_uint8_t data[2] = {reg, value}, actual = 0;
    struct rt_i2c_msg msg = {.addr=TPS_ADDR, .flags=RT_I2C_WR, .len=2, .buf=data};
    if (!bus || rt_i2c_transfer(bus, &msg, 1) != 1 ||
        read_reg(reg, &actual) != RT_EOK || actual != value)
    {
        rt_kprintf("[TPS] register 0x%02x verify failed: want=%02x got=%02x\n", reg, value, actual);
        return -RT_ERROR;
    }
    return RT_EOK;
}

static rt_err_t restore_config(void)
{
    /* WAKEUP low resets volatile registers. Restore before PWRUP, never EEPROM. */
    if (write_checked(0x09, 0xb1) || write_checked(0x0a, 0x00) ||
        write_checked(0x0b, 0x4e) || write_checked(0x0c, 0x84) ||
        write_checked(REG_VADJ, 0x03) ||
        write_checked(REG_VCOM1, (saved_vcom_mv / 10) & 0xff) ||
        write_checked(REG_VCOM2, (saved_vcom_mv / 10) >> 8))
        return -RT_ERROR;
    configured = RT_TRUE;
    return RT_EOK;
}

rt_err_t tps_init(uint16_t vcom_mv)
{
    rt_uint8_t id = 0;
    struct rt_i2c_configuration cfg = {.mode=0, .addr=0, .timeout=500, .max_hz=400000};
    configured = powered = RT_FALSE;
    saved_vcom_mv = vcom_mv;
    rt_pin_mode(EPD_TPS_WAKEUP_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(EPD_TPS_PWRCOM_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(EPD_TPS_PWRUP_PIN, PIN_MODE_OUTPUT);
#ifdef EPD_TPS_GOOD_PIN
    /* GPIO mode does not configure PAD pulls in this SDK. */
    HAL_PIN_Set(PAD_PA00 + EPD_TPS_GOOD_PIN, GPIO_A0 + EPD_TPS_GOOD_PIN, PIN_PULLUP, 1);
    rt_pin_mode(EPD_TPS_GOOD_PIN, PIN_MODE_INPUT_PULLUP);
#endif
    rt_pin_write(EPD_TPS_PWRCOM_PIN, 0);
    rt_pin_write(EPD_TPS_PWRUP_PIN, 0);
    rt_thread_mdelay(150);
    rt_pin_write(EPD_TPS_WAKEUP_PIN, 0);
    rt_thread_mdelay(10);
    rt_pin_write(EPD_TPS_WAKEUP_PIN, 1);
    rt_thread_mdelay(10);

    bus = (struct rt_i2c_bus_device *)rt_device_find(EPD_TPS_I2C_DEVICE_NAME);
    if (!bus || rt_device_open((rt_device_t)bus,
        RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_TX | RT_DEVICE_FLAG_INT_RX) != RT_EOK ||
        rt_i2c_configure(bus, &cfg) != RT_EOK || read_reg(REG_REVID, &id) != RT_EOK)
    {
        rt_kprintf("[TPS] init failed on %s addr=0x68\n", EPD_TPS_I2C_DEVICE_NAME);
        power_errors++;
        return -RT_ERROR;
    }
    /* Preserve reference panel sequences: up VGL,VNEG,VGH,VPOS; reverse down.
     * Only volatile registers are written; never program the PMIC EEPROM. */
    if ((id & 0x0f) != 5 || vcom_mv > 5110 || restore_config())
    {
        rt_kprintf("[TPS] config failed, REVID=0x%02x\n", id);
        power_errors++;
        return -RT_ERROR;
    }
    configured = RT_TRUE;
    rt_kprintf("[TPS] READY revid=0x%02x bus=%s WAKE=%d COM=%d UP=%d VCOM=-%dmV\n",
               id, EPD_TPS_I2C_DEVICE_NAME, EPD_TPS_WAKEUP_PIN,
               EPD_TPS_PWRCOM_PIN, EPD_TPS_PWRUP_PIN, vcom_mv);
    return RT_EOK;
}

rt_err_t tps_enter_sleep(void)
{
    rt_pin_write(EPD_TPS_PWRCOM_PIN, 0);
    rt_thread_mdelay(10);
    rt_pin_write(EPD_TPS_PWRUP_PIN, 0);
    powered = RT_FALSE;
    /* Finish the existing discharge interval before entering true SLEEP. */
    rt_thread_mdelay(150);
    rt_pin_write(EPD_TPS_WAKEUP_PIN, 0);
    configured = RT_FALSE;
    pgood_pull(0); /* PGOOD is low while off: no DC current through MCU pull-up. */
    return RT_EOK;
}

rt_err_t tps_exit_sleep(void)
{
    rt_uint8_t pg = 0;
    rt_tick_t start;
    if (!bus) return -RT_ERROR;
    if (powered) return RT_EOK;
    pgood_pull(1);
    if (!configured)
    {
        rt_pin_write(EPD_TPS_WAKEUP_PIN, 1);
        rt_thread_mdelay(10);
        if (restore_config() != RT_EOK)
        {
            ++power_errors;
            tps_enter_sleep();
            return -RT_ERROR;
        }
    }
    start = rt_tick_get();
    rt_pin_write(EPD_TPS_PWRUP_PIN, 1);
    while ((rt_tick_get() - start) < rt_tick_from_millisecond(500))
    {
        if (read_reg(REG_PG, &pg) != RT_EOK) break;
        if ((pg & PG_ALL_RAILS) == PG_ALL_RAILS
#ifdef EPD_TPS_GOOD_PIN
            && rt_pin_read(EPD_TPS_GOOD_PIN)
#endif
           )
        {
            rt_pin_write(EPD_TPS_PWRCOM_PIN, 1);
            rt_thread_mdelay(10);
            powered = RT_TRUE;
            power_cycles++;
            {
                uint8_t enable=0, int1=0, int2=0;
                read_reg(0x01, &enable); read_reg(0x07, &int1); read_reg(0x08, &int2);
                rt_kprintf("[TPS] POWER_OK PG=0x%02x cycle=%u ENABLE=%02x INT=%02x:%02x\n",
                           pg, power_cycles, enable, int1, int2);
            }
            return RT_EOK;
        }
        rt_thread_mdelay(5);
    }
    power_errors++;
    rt_kprintf("[TPS] POWER_FAIL PG=0x%02x; stopping high voltage\n", pg);
    tps_enter_sleep();
    return -RT_ERROR;
}

static int epd_power(int argc, char **argv)
{
    rt_uint8_t id = 0, pg = 0, v1 = 0, v2 = 0;
    int rc;
    (void)argc;
    (void)argv;
    rc = read_reg(REG_REVID, &id) | read_reg(REG_PG, &pg) |
         read_reg(REG_VCOM1, &v1) | read_reg(REG_VCOM2, &v2);
    rt_kprintf("EPD_POWER rc=%d revid=%02x pg=%02x VCOM=-%umV configured=%d powered=%d cycles=%u errors=%u\n",
               rc, id, pg, (((v2 & 1) << 8) | v1) * 10,
               configured, powered, power_cycles, power_errors);
    rt_kprintf("EPD_PINS wake=%d com=%d up=%d",
               rt_pin_read(EPD_TPS_WAKEUP_PIN), rt_pin_read(EPD_TPS_PWRCOM_PIN),
               rt_pin_read(EPD_TPS_PWRUP_PIN));
#ifdef EPD_TPS_GOOD_PIN
    rt_kprintf(" good=%d", rt_pin_read(EPD_TPS_GOOD_PIN));
#endif
    rt_kprintf("\n");
    return rc;
}
MSH_CMD_EXPORT(epd_power, Read EPD power status and counters);
