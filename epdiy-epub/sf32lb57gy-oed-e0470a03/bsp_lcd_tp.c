/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp_board.h"

static void set_lcd_pin(int pad, pin_function function)
{
    GPIO_InitTypeDef input = {.Pin=pad-PAD_PA00, .Mode=GPIO_MODE_INPUT, .Pull=GPIO_NOPULL};
    HAL_GPIO_Init(hwp_gpio1, &input); /* Release the idle GPIO output override. */
    HAL_PIN_SetMode(pad, 1, PIN_DIGITAL_IO_NORMAL);
    int result = HAL_PIN_Set(pad, function, PIN_NOPULL, 1);
    HAL_ASSERT(result == 0);
    (void)result;
}

void BSP_EPD_BusIdle(void)
{
    static const uint8_t pins[] = {24,25,26,27,28,29,30,31,32,33,37,38,39,40,41};
    unsigned i;
    for (i=0; i<sizeof(pins); ++i) {
        HAL_PIN_Set(PAD_PA00+pins[i], GPIO_A0+pins[i], PIN_NOPULL, 1);
        BSP_GPIO_Set(pins[i], 0, 1);
        HAL_PIN_SetMode(PAD_PA00+pins[i], 1, PIN_DIGITAL_O_NORMAL);
    }
}

void BSP_LCD_Reset(uint8_t level)
{
    (void)level; /* E0470A03 raw panel has no reset pin. */
}

void BSP_LCD_PowerUp(void)
{
    /* EPD D0..D7 map to the low eight TCON source data outputs. */
    set_lcd_pin(PAD_PA24, LCDC1_TCON_SDCLK);
    set_lcd_pin(PAD_PA25, LCDC1_TCON_SDLE);
    set_lcd_pin(PAD_PA26, LCDC1_TCON_SDOE);
    set_lcd_pin(PAD_PA27, LCDC1_TCON_SDSTL);
    set_lcd_pin(PAD_PA28, LCDC1_TCON_SDAT0);
    set_lcd_pin(PAD_PA29, LCDC1_TCON_SDAT1);
    set_lcd_pin(PAD_PA30, LCDC1_TCON_SDAT2);
    set_lcd_pin(PAD_PA31, LCDC1_TCON_SDAT3);
    set_lcd_pin(PAD_PA32, LCDC1_TCON_SDAT4);
    set_lcd_pin(PAD_PA33, LCDC1_TCON_SDAT5);
    set_lcd_pin(PAD_PA37, LCDC1_TCON_SDAT6);
    set_lcd_pin(PAD_PA38, LCDC1_TCON_SDAT7);
    set_lcd_pin(PAD_PA40, LCDC1_TCON_GDCLK);
    set_lcd_pin(PAD_PA41, LCDC1_TCON_GDSP);
    HAL_PIN_Set(PAD_PA39, GPIO_A39, PIN_NOPULL, 1);
    BSP_GPIO_Set(39, 0, 1);
    /* SF57 TCON clock selector: 0 selects SYSCLK. SDK v2.5 removed the
     * RCC_CLK_MOD_LCDC alias; the family register field remains unchanged. */
    HAL_RCC_HCPU_ClockSelect(HPSYS_RCC_CSR_SEL_LCDC_Pos, 0);
}

void BSP_LCD_GMODE_Set(uint8_t level)
{
    BSP_GPIO_Set(39, level, 1);
}

void BSP_LCD_PowerDown(void)
{
    BSP_GPIO_Set(43, 0, 1);
    BSP_GPIO_Set(44, 0, 1);
    /* Keep WAKEUP high so the TPS completes its programmed power-down. */
    BSP_GPIO_Set(39, 0, 1);
}

void BSP_TP_PowerUp(void)
{
    BSP_GPIO_Set(5, 1, 1);
    BSP_GPIO_Set(4, 1, 1);
    HAL_PIN_Set(PAD_PA03, GPIO_A3, PIN_PULLUP, 1);
}

void BSP_TP_PowerDown(void)
{
    BSP_GPIO_Set(4, 0, 1);
    BSP_GPIO_Set(5, 0, 1);
}

void BSP_TP_Reset(uint8_t level)
{
    BSP_GPIO_Set(4, level, 1);
}

uint32_t BSP_GET_DPI_AUX_VSYNC_PIN(void)
{
    return 41;
}
