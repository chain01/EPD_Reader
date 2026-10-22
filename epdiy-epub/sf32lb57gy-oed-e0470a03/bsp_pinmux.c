/* SPDX-License-Identifier: Apache-2.0
 * DevKit EPD: explicit idle states for the power measurement workload.
 */
#include "bsp_board.h"

static void output_low(int pin)
{
    HAL_GPIO_WritePin(hwp_gpio1, pin, GPIO_PIN_RESET);
    HAL_PIN_Set(PAD_PA00+pin, GPIO_A0+pin, PIN_NOPULL, 1);
    BSP_GPIO_Set(pin, 0, 1);
    HAL_PIN_SetMode(PAD_PA00+pin, 1, PIN_DIGITAL_O_NORMAL);
}

static void input_pin(int pin, int pull, int analog)
{
    GPIO_InitTypeDef cfg = {.Pin=pin, .Mode=GPIO_MODE_INPUT, .Pull=GPIO_NOPULL};
    HAL_GPIO_Init(hwp_gpio1, &cfg);
    HAL_PIN_Set(PAD_PA00+pin, GPIO_A0+pin, pull, 1);
    HAL_PIN_SetMode(PAD_PA00+pin, 1, analog ? PIN_ANALOG_INPUT :
                   (pull==PIN_PULLUP ? PIN_DIGITAL_IO_PULLUP : PIN_DIGITAL_IO_NORMAL));
}

void BSP_UART_PinsIdle(void)
{
    input_pin(18, PIN_NOPULL, 1);
    input_pin(19, PIN_NOPULL, 1);
}

void BSP_UART_PinsActive(void)
{
    input_pin(18, PIN_NOPULL, 0);
    input_pin(19, PIN_NOPULL, 0);
    HAL_PIN_Set(PAD_PA18, USART1_RXD, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA19, USART1_TXD, PIN_NOPULL, 1);
}

void BSP_PIN_Init(void)
{
#ifdef SOC_BF0_HCPU
    unsigned i;
    static const uint8_t powered_off_inputs[] = {1,2,3,7,11,12,13,15,16,17,22,23,34,35,36,56};
    static const uint8_t low_outputs[] = {4,5,8,9,10,14,24,25,26,27,28,29,30,31,32,33,
                                         37,38,39,40,41,42,43,44,47,53,54,55,57};
    for (i=0; i<sizeof(low_outputs); ++i) output_low(low_outputs[i]);
    for (i=0; i<sizeof(powered_off_inputs); ++i) input_pin(powered_off_inputs[i], PIN_NOPULL, 1);
    input_pin(0, PIN_NOPULL, 1); /* BOOT: do not drive. */
    input_pin(6, PIN_NOPULL, 1); /* USB_DET: external divider, never pull low. */
    BSP_UART_PinsActive();

    /* Shared powered I2C: external R39/R40 pull up to 3.3 V. No internal pulls. */
    input_pin(20, PIN_NOPULL, 0);
    input_pin(21, PIN_NOPULL, 0);
    HAL_PIN_Set(PAD_PA20, I2C1_SCL, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA21, I2C1_SDA, PIN_NOPULL, 1);
    input_pin(52, PIN_NOPULL, 1); /* PGOOD pull-up enabled only during TPS use. */

    /* TF is always powered. CLK low; CMD already has R57=10k pull-up.
     * DAT lines are inputs with weak pulls, never driven against the card.
     * Card-detect R56 is external: no duplicate MCU pull-up. */
    input_pin(45, PIN_PULLUP, 0);
    input_pin(46, PIN_PULLUP, 0);
    input_pin(48, PIN_NOPULL, 0);
    input_pin(49, PIN_PULLUP, 0);
    input_pin(50, PIN_PULLUP, 0);
    input_pin(51, PIN_NOPULL, 0);
#ifdef BSP_USING_SDMMC2
    HAL_PIN_Set(PAD_PA45, SD2_DIO2, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA46, SD2_DIO3, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA47, SD2_CLK, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA48, SD2_CMD, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA49, SD2_DIO0, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA50, SD2_DIO1, PIN_PULLUP, 1);
#endif
#ifdef BSP_USING_I2C2
    HAL_PIN_Set(PAD_PA01, I2C2_SCL, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA02, I2C2_SDA, PIN_NOPULL, 1);
#endif
#endif
}
