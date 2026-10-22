/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp_board.h"
#if defined(BSP_USING_RTTHREAD) && !defined(CFG_BOOTLOADER)
#include <rtthread.h>

int epd_pins(int argc, char **argv)
{
    static const struct { int pin; pin_function function; const char *name; } map[] = {
        {24, LCDC1_TCON_SDCLK, "CLK"}, {25, LCDC1_TCON_SDLE, "LE"},
        {26, LCDC1_TCON_SDOE, "OE"}, {27, LCDC1_TCON_SDSTL, "SPH"},
        {28, LCDC1_TCON_SDAT0, "D0"}, {29, LCDC1_TCON_SDAT1, "D1"},
        {30, LCDC1_TCON_SDAT2, "D2"}, {31, LCDC1_TCON_SDAT3, "D3"},
        {32, LCDC1_TCON_SDAT4, "D4"}, {33, LCDC1_TCON_SDAT5, "D5"},
        {37, LCDC1_TCON_SDAT6, "D6"}, {38, LCDC1_TCON_SDAT7, "D7"},
        {39, GPIO_A39, "GMODE"}, {40, LCDC1_TCON_GDCLK, "CPV"},
        {41, LCDC1_TCON_GDSP, "STV"}, {42, GPIO_A42, "WAKEUP"},
        {43, GPIO_A43, "PWRCOM"}, {44, GPIO_A44, "PWRUP"},
        {52, GPIO_A52, "PGOOD"},
        {20, I2C1_SCL, "TPS_SCL"}, {21, I2C1_SDA, "TPS_SDA"},
    };
    unsigned int i, errors = 0;
    (void)argc;
    (void)argv;
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
    {
        pin_function actual = PIN_FUNC_UNDEF;
        PIN_ModeTypeDef mode;
        int result = HAL_PIN_Get(PAD_PA00 + map[i].pin, &actual, &mode, 1);
        int ok = result >= 0 && actual == map[i].function;
        errors += !ok;
        if(argc||!ok)rt_kprintf("EPD_PIN PA%02d %-7s func=%d expected=%d %s\n",
                   map[i].pin, map[i].name, actual, map[i].function, ok ? "OK" : "FAIL");
    }
    {
        unsigned int output = HAL_GPIO_GetMode(hwp_gpio1, 52) != GPIO_MODE_INPUT;
        rt_uint32_t mux = ((volatile rt_uint32_t *)PINMUX1_BASE)[PAD_PA52 - 1];
        unsigned int pullup = (mux & (HPSYS_PINMUX_PAD_PA00_PE_Msk |
                            HPSYS_PINMUX_PAD_PA00_PS_Msk)) ==
                            (HPSYS_PINMUX_PAD_PA00_PE_Msk | HPSYS_PINMUX_PAD_PA00_PS_Msk);
        errors += output || !pullup;
        if(argc||output||!pullup)rt_kprintf("EPD_PGOOD input=%u pullup=%u\n", !output, pullup);
    }
    if(argc||errors)rt_kprintf("EPD_PINCHECK checked=21 errors=%u\n", errors);
    return errors ? -RT_ERROR : RT_EOK;
}
MSH_CMD_EXPORT(epd_pins, Check EPD pin function mapping against schematic);

static int epd_tick_clock(void)
{
    HAL_RCC_HCPU_ClockSelect(RCC_CLK_MOD_HP_TICK, RCC_CLK_TICK_HXT48);
    return 0;
}
INIT_BOARD_EXPORT(epd_tick_clock);
#endif
