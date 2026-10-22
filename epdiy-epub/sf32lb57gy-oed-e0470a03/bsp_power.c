/* SPDX-License-Identifier: Apache-2.0
 * All functions/rodata in this object are linked into internal RAM.
 * The SDK DEEP handler masks IRQs before these hooks; do not call RTOS APIs here.
 */
#include "bsp_board.h"
#ifdef BSP_USING_RTTHREAD
#include <rtthread.h>
#include "drv_flash.h"
#endif

extern void BSP_UART_PinsIdle(void);
extern void BSP_UART_PinsActive(void);
static FLASH_HandleTypeDef *nor[2];
static uint8_t nor_asleep[2];
static uint8_t psram_enabled, psram_asleep, initialized;
volatile uint32_t epd_pm_enters, epd_pm_exits, epd_pm_errors;

void BSP_GPIO_Set(int pin, int val, int is_porta)
{
    GPIO_TypeDef *gpio = is_porta ? hwp_gpio1 : hwp_gpio2;
    GPIO_InitTypeDef cfg = {.Pin=pin, .Mode=GPIO_MODE_OUTPUT, .Pull=GPIO_NOPULL};
    HAL_GPIO_WritePin(gpio, pin, (GPIO_PinState)val);
    HAL_GPIO_Init(gpio, &cfg);
}

void BSP_LowPowerInit(void)
{
#ifdef BSP_USING_RTTHREAD
    unsigned i;
    uint8_t type = bsp_psram_get_mpi_mode(1);
    psram_enabled = type==SPI_MODE_OPSRAM || type==SPI_MODE_HPSRAM ||
                    type==SPI_MODE_LEGPSRAM || type==SPI_MODE_HBPSRAM;
    for (i=0; i<2; ++i) {
        uint32_t base = i ? MPI3_MEM_BASE : MPI2_MEM_BASE;
        FLASH_HandleTypeDef *h = (FLASH_HandleTypeDef *)rt_flash_get_handle_by_addr(base);
        uint32_t id = 0;
        if (h && h->ctable && !h->isNand && h->size) {
            id = HAL_FLASH_GET_NOR_ID(h) & 0xffffffu;
            /* Measured SiP NOR ID; SDK flash_table.c identifies PY25Q128HA.
             * Do not issue DPD to an unrecognized/absent device. */
            if (id==0x182085u &&
                h->ctable->cmd_cfg[SPI_FLASH_CMD_DPD].cmd==0xb9 &&
                h->ctable->cmd_cfg[SPI_FLASH_CMD_RDP].cmd==0xab) nor[i]=h;
        }
        rt_kprintf("LP_FLASH mpi=%u id=%06x dpd=%u\n", i+2, id, nor[i]!=0);
    }
    initialized = 1;
    rt_kprintf("LP_MEMORY psram_mode=%u retention_sleep=%u\n", type, psram_enabled);
#endif
}

void BSP_Power_Up(bool is_deep_sleep)
{
    unsigned i;
    (void)is_deep_sleep;
    if (!initialized) return;
    /* Restore NOR before any flash-resident code or constants may be used. */
    for (i=0; i<2; ++i) if (nor_asleep[i]) {
        if (HAL_FLASH_RELEASE_DPD(nor[i])) ++epd_pm_errors;
        HAL_Delay_us(100);
        nor_asleep[i]=0;
    }
    if (psram_asleep) {
        if (bsp_psram_exit_low_power("psram1")) ++epd_pm_errors;
        psram_asleep=0;
    }
    BSP_UART_PinsActive();
    ++epd_pm_exits;
}

void BSP_IO_Power_Down(int coreid, bool is_deep_sleep)
{
    unsigned i;
    (void)is_deep_sleep;
    if (!initialized || coreid!=CORE_ID_HCPU) return;
    BSP_UART_PinsIdle();
    /* Persist dirty PSRAM lines before its retained sleep command. */
    SCB_CleanDCache();
    __DSB();
    if (psram_enabled) {
        if (bsp_psram_enter_low_power("psram1")) ++epd_pm_errors;
        else psram_asleep=1;
    }
    ++epd_pm_enters;
    /* NOR last; only internal-RAM code runs until BSP_Power_Up releases it. */
    for (i=0; i<2; ++i) if (nor[i]) {
        if (HAL_FLASH_DEEP_PWRDOWN(nor[i])) ++epd_pm_errors;
        else nor_asleep[i]=1;
        HAL_Delay_us(10);
    }
}
