/*
 * SPDX-FileCopyrightText: 2026 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include "string.h"
#include "board.h"
#include "drv_io.h"
#include "drv_lcd.h"

#include "epd_tps.h"
#include "mem_section.h"
#include "epd_waveform.h"
#include "epd_power_status.h"
#include "system_bf0_ap.h"

/**
  * @brief epd_opm060da chip IDs
  */
#define THE_LCD_ID                  0x19357

#define  DBG_LEVEL            DBG_ERROR  //DBG_LOG //
#define LOG_TAG                "epd_e0470a03_57"
#include "log.h"
enum EpdRotation
{
    EPD_ROT_LANDSCAPE = 0,
    EPD_ROT_PORTRAIT = 1,
    EPD_ROT_INVERTED_LANDSCAPE = 2,
    EPD_ROT_INVERTED_PORTRAIT = 3,
};


#if (EPD_PANEL_HOR == LCD_VER_RES_MAX) && (EPD_PANEL_VER == LCD_HOR_RES_MAX)
    #define DISPLAY_ROTATE EPD_ROT_INVERTED_PORTRAIT
#else
    #define DISPLAY_ROTATE EPD_ROT_LANDSCAPE
#endif


static enum EpdRotation display_rotation = EPD_ROT_LANDSCAPE;
/* Panel native (TCON output) resolution */
#define EPD_PANEL_HOR 1216
#define EPD_PANEL_VER 684



static const LCDC_InitTypeDef lcdc_int_cfg_edp_16bit =
{
    .lcd_itf = LCDC_INTF_EPD_8BIT,
#ifdef BSP_USING_BOARD_SF32LB57GY_OED_E0470A03
    .freq = 48 * 1000 * 1000,
#else
    .freq = 24 * 1000 * 1000,
#endif
    .color_mode = LCDC_PIXEL_FORMAT_F2_SWAP,

    .cfg = {
        .epd = {
#if defined(BSP_USING_BOARD_SF32LB57GY_OED_E0470A03)
            .SDMODE = 0,
#elif defined(EPD_WAVEFORM_USE_BIN)
            .SDMODE = 1, //Source driver mode (BIN waveform requires SDMODE=1)
#else
            .SDMODE = 0, //Source driver mode
#endif
            .SDCLK_polarity = 0, //Source driver clock polarity
            .GDSP_polarity = 0,
#ifdef BSP_USING_BOARD_SF32LB57GY_OED_E0470A03
            .GDCLK_polarity = 1, // E0470A03-AF-S gate scan edge; verified on new DevKit.
#else
            .GDCLK_polarity = 0,
#endif

#ifdef BSP_USING_BOARD_SF32LB57GY_OED_E0470A03
            .LSL = 20, // 417 ns latch pulse at 48 MHz (panel minimum 300 ns).
            .LBL = 48, // 1 us latch-off to source-start (panel minimum 800 ns).
            .LEL = 24, // 500 ns, at least ten dummy clocks as required by the panel.
#else
            .LSL = 4,
            .LBL = 1,
            .LEL = 3,
#endif
            .LDL = EPD_PANEL_HOR >> 2, //Line data length:

            .GSTA = 3, //Gate STA length

            .FSL = 1, //Frame sync length
            .FBL = 3, //Frame begin length    100ns  ->
            .FDL = EPD_PANEL_VER, //Frame data length (684 rows)
            .FEL = 1, //Frame end length

        },
    },

};

static LCDC_InitTypeDef lcdc_int_cfg;
static rt_bool_t panel_power_ready;
static volatile rt_bool_t drawing;
static void  LCD_WriteReg(LCDC_HandleTypeDef *hlcdc, uint16_t LCD_Reg, uint8_t *Parameters, uint32_t NbParameters);
static uint32_t LCD_ReadData(LCDC_HandleTypeDef *hlcdc, uint16_t RegValue, uint8_t ReadSize);










/**
  * @brief  Power on the LCD.
  * @param  None
  * @retval None
  */
static void LCD_Init(LCDC_HandleTypeDef *hlcdc)
{
    uint8_t parameter[32];

    memcpy(&lcdc_int_cfg, &lcdc_int_cfg_edp_16bit, sizeof(lcdc_int_cfg));
    memcpy(&hlcdc->Init, &lcdc_int_cfg, sizeof(LCDC_InitTypeDef));
    HAL_LCDC_Init(hlcdc);

    BSP_LCD_Reset(1);
    rt_thread_mdelay(10);
    BSP_LCD_Reset(0);//Reset LCD
    rt_thread_mdelay(10);
    BSP_LCD_Reset(1);
    rt_thread_mdelay(10);


    HAL_LCDC_SetROIArea(hlcdc, 0, 0, EPD_PANEL_HOR - 1, EPD_PANEL_VER - 1);

    display_rotation = DISPLAY_ROTATE;

    epd_wave_table();

#ifdef EPD_WAVEFORM_USE_BIN
#ifdef EPD_VCOM_MV
    panel_power_ready = (tps_init(EPD_VCOM_MV) == RT_EOK);
#else
    panel_power_ready = (tps_init(2100) == RT_EOK);
#endif
#else
    panel_power_ready = (tps_init(1000) == RT_EOK);
#endif
    if (panel_power_ready)
        panel_power_ready = (tps_exit_sleep() == RT_EOK);
#ifdef BSP_USING_BOARD_SF32LB57GY_OED_E0470A03
    {
        extern int epd_pins(int argc, char **argv);
        epd_pins(0, RT_NULL);
    }
#endif
}


/**
  * @brief  Disables the Display.
  * @param  None
  * @retval LCD Register Value.
  */
static uint32_t LCD_ReadID(LCDC_HandleTypeDef *hlcdc)
{
    return THE_LCD_ID;
}

/**
  * @brief  Enables the Display.
  * @param  None
  * @retval None
  */
static void LCD_DisplayOn(LCDC_HandleTypeDef *hlcdc)
{
    /* Display On */
}

/**
  * @brief  Disables the Display.
  * @param  None
  * @retval None
  */
static void LCD_DisplayOff(LCDC_HandleTypeDef *hlcdc)
{
    /* Display Off */
}

static void LCD_SetRegion(LCDC_HandleTypeDef *hlcdc, uint16_t Xpos0, uint16_t Ypos0, uint16_t Xpos1, uint16_t Ypos1)
{

}

/**
  * @brief  Writes pixel.
  * @param  Xpos: specifies the X position.
  * @param  Ypos: specifies the Y position.
  * @param  RGBCode: the RGB pixel color
  * @retval None
  */
static void LCD_WritePixel(LCDC_HandleTypeDef *hlcdc, uint16_t Xpos, uint16_t Ypos, const uint8_t *RGBCode)
{

}

/*
Define a mixed grey framebuffer on PSRAM
high 4 bits for old pixel and low 4 bits for new pixel in every byte.
*/
#ifdef BSP_USING_PSRAM2
L2_NON_RET_BSS_SECT2_BEGIN(frambuf)
L2_NON_RET_BSS_SECT2(frambuf, ALIGN(64) static uint8_t mixed_framebuffer[EPD_PANEL_HOR * EPD_PANEL_VER]);
L2_NON_RET_BSS_SECT2_END
#else
L2_NON_RET_BSS_SECT_BEGIN(frambuf)
L2_NON_RET_BSS_SECT(frambuf, ALIGN(64) static uint8_t mixed_framebuffer[EPD_PANEL_HOR * EPD_PANEL_VER]);
L2_NON_RET_BSS_SECT_END
#endif

L1_RET_CODE_SECT(epd_codes, static void CopyToMixedGrayBuffer(LCDC_HandleTypeDef *hlcdc, const uint8_t *RGBCode, uint16_t Xpos0, uint16_t Ypos0, uint16_t Xpos1, uint16_t Ypos1))
{
    RT_ASSERT(LCD_HOR_RES_MAX == (Xpos1 - Xpos0 + 1));
    RT_ASSERT(LCD_VER_RES_MAX == (Ypos1 - Ypos0 + 1));

    LOG_D("[EPD] flush: %s, src=%dx%d\n",
               display_rotation == EPD_ROT_INVERTED_PORTRAIT ? "PORTRAIT(rot)" :
               display_rotation == EPD_ROT_LANDSCAPE ? "LANDSCAPE" :
               display_rotation == EPD_ROT_PORTRAIT ? "PORTRAIT" : "INV_LANDSCAPE",
               Xpos1 - Xpos0 + 1, Ypos1 - Ypos0 + 1);

    //Convert layer data to 4bit gray data
    if (hlcdc->Layer[HAL_LCDC_LAYER_DEFAULT].data_format == LCDC_PIXEL_FORMAT_MONO)
    {
        RT_ASSERT(0);//Not implemented yet
    }
    else if (hlcdc->Layer[HAL_LCDC_LAYER_DEFAULT].data_format == LCDC_PIXEL_FORMAT_A4)
    {
        const uint8_t *p_src = RGBCode;
        uint8_t *p_dst = mixed_framebuffer;

        if (display_rotation == EPD_ROT_INVERTED_PORTRAIT)
        {

            uint16_t src_w = LCD_HOR_RES_MAX; // 758

            for (uint16_t x = Xpos0; x <= Xpos1; x++)
            {
                uint16_t sx = x - Xpos0;
                uint8_t *p_dst_row = &p_dst[(LCD_HOR_RES_MAX - 1 - x) * EPD_PANEL_HOR];

                for (uint16_t y = Ypos0; y <= Ypos1; y++)
                {
                    uint16_t sy = y - Ypos0;
                    uint8_t src_byte = p_src[sy * (src_w / 2) + sx / 2];
                    uint8_t gray = (sx & 1) ? (src_byte >> 4) : (src_byte & 0x0F);

                    uint8_t *p_dst_pixel = &p_dst_row[y];
                    *p_dst_pixel = ((*p_dst_pixel & 0x0F) << 4) | gray;
                }
            }
        }
        else
        {
            uint32_t n = LCD_HOR_RES_MAX * LCD_VER_RES_MAX / 4; // 每次处理4像素（4字节）
            uint32_t *p_dst32 = (uint32_t *)(mixed_framebuffer);

            while (n--)
            {
                uint8_t byte0 = *p_src++;
                uint8_t byte1 = *p_src++;

                // 生成4像素的新值
                uint32_t src_v = ((byte1 << 20) | (byte1 << 16) | (byte0 << 4) | byte0) & 0x0F0F0F0F;

                // 读取原像素，旧像素清零，新像素移入老像素
                uint32_t dst_v = (*p_dst32 & 0x0F0F0F0F) << 4;

                // 合并新像素
                *p_dst32++ = dst_v | src_v;
            }

            //16bit 特殊处理
            // uint32_t n = LCD_HOR_RES_MAX * LCD_VER_RES_MAX / 8; // 每次处理8像素（8字节）
            // uint64_t *p_dst64 = (uint64_t *)(mixed_framebuffer);

            // while (n--)
            // {
            //     uint8_t byte0 = *p_src++;
            //     uint8_t byte1 = *p_src++;
            //     uint8_t byte2 = *p_src++;
            //     uint8_t byte3 = *p_src++;


            //     // 生成8像素的新值
            //     uint32_t src_v_1 = ((byte1 << 20) | (byte1 << 16) | (byte0 << 4) | byte0) & 0x0F0F0F0F;
            //     uint32_t src_v_2 = ((byte3 << 20) | (byte3 << 16) | (byte2 << 4) | byte2) & 0x0F0F0F0F;

            //     uint64_t src_v = ((uint64_t)src_v_1 << 32) | src_v_2;
            //     // 读取原像素，旧像素清零，新像素移入老像素
            //     uint64_t dst_v = (*p_dst64 & 0x0F0F0F0F0F0F0F0FULL) << 4;

            //     // 合并新像素
            //     *p_dst64++ = dst_v | src_v;

        }
    }
    else if (hlcdc->Layer[HAL_LCDC_LAYER_DEFAULT].data_format == LCDC_PIXEL_FORMAT_RGB565)
    {
        // 计算灰度值
        // 0.299*R + 0.587*G + 0.114*B
#define RGB565_TO_GRAY4(rgb)  ( \
        (uint8_t)(( \
        ((((rgb) >> 8) & 0xF8) * 77 + \
         (((rgb) >> 3) & 0xFC) * 150 + \
         (((rgb) << 3) & 0xF8) * 29) >> 8) >> 4) \
        )

        if (display_rotation == EPD_ROT_INVERTED_PORTRAIT)
        {
            // Rotate LVGL portrait 758x1024 → panel landscape 1024x758
            // Source pixel (x, y) → dest pixel (y, 757-x)
            RT_ASSERT(((Ypos1 - Ypos0 + 1) % 4) == 0);
            RT_ASSERT((Ypos0 % 4) == 0);

            const uint16_t *p_src = (const uint16_t *)RGBCode;
            uint8_t *p_dst = mixed_framebuffer;

            for (uint16_t x = Xpos0; x < Xpos1; x++)
            {
                for (uint16_t y = Ypos0; y < Ypos1; y += 4)
                {
                    uint8_t gray0 = RGB565_TO_GRAY4(p_src[(y - Ypos0) * (Xpos1 - Xpos0 + 1) + (x - Xpos0)]);
                    uint8_t gray1 = RGB565_TO_GRAY4(p_src[(y + 1 - Ypos0) * (Xpos1 - Xpos0 + 1) + (x - Xpos0)]);
                    uint8_t gray2 = RGB565_TO_GRAY4(p_src[(y + 2 - Ypos0) * (Xpos1 - Xpos0 + 1) + (x - Xpos0)]);
                    uint8_t gray3 = RGB565_TO_GRAY4(p_src[(y + 3 - Ypos0) * (Xpos1 - Xpos0 + 1) + (x - Xpos0)]);

                    uint16_t dst_x0 = y;
                    uint16_t dst_y0 = LCD_HOR_RES_MAX - 1 - x;

                    uint32_t dst_index = dst_y0 * EPD_PANEL_HOR + dst_x0;
                    uint32_t old_val = *((uint32_t *)(p_dst + dst_index));

                    uint32_t new_val =
                        (((old_val >> 0) & 0x0F) << 4)  | gray0 |
                        (((old_val >> 8) & 0x0F) << 12) | (gray1 << 8) |
                        (((old_val >> 16) & 0x0F) << 20) | (gray2 << 16) |
                        (((old_val >> 24) & 0x0F) << 28) | (gray3 << 24);

                    *((uint32_t *)(p_dst + dst_index)) = new_val;
                }
            }
        }
        else
        {
            uint32_t n = LCD_HOR_RES_MAX * LCD_VER_RES_MAX / 4; // 每次处理4像素（4字节）
            uint32_t *p_dst = (uint32_t *)(mixed_framebuffer);
            const uint16_t *p_src = (const uint16_t *)RGBCode;

            while (n--)
            {
                uint8_t pixel0 = RGB565_TO_GRAY4(*p_src);
                p_src++;
                uint8_t pixel1 = RGB565_TO_GRAY4(*p_src);
                p_src++;
                uint8_t pixel2 = RGB565_TO_GRAY4(*p_src);
                p_src++;
                uint8_t pixel3 = RGB565_TO_GRAY4(*p_src);
                p_src++;


                // 生成4像素的新值
                uint32_t src_v = ((pixel3 << 24) | (pixel2 << 16) | (pixel1 << 8) | pixel0) & 0x0F0F0F0F;

                // 读取原像素，旧像素清零，新像素移入老像素
                uint32_t dst_v = (*p_dst & 0x0F0F0F0F) << 4;

                // 合并新像素
                *p_dst++ = dst_v | src_v;
            }
        }

#undef RGB565_TO_GRAY4
    }
    else
        RT_ASSERT(0);
}


static void (* Ori_XferCpltCallback)(struct __LCDC_HandleTypeDef *lcdc);
static uint32_t total_frames; //Total frames need to do
static uint32_t curr_frame; //Current flushing frame index
static HAL_LCDC_PixelFormat ori_format;
static uint32_t lut[HAL_LCDC_LOOKUP_TABLE_SIZE >> 2];
static volatile uint32_t completed_draws;
static volatile uint32_t completed_frames;
static volatile uint32_t transfer_errors;
static uint32_t StartFrame(LCDC_HandleTypeDef *hlcdc, uint32_t frame_idx)
{
    if (frame_idx < total_frames)
    {
        epd_wave_table_fill_lut((uint32_t *)&lut[0], frame_idx);
        BSP_LCD_GMODE_Set(0);
        HAL_Delay_us(1);
        BSP_LCD_GMODE_Set(1);
        if (HAL_LCDC_SendLayerData_IT(hlcdc) != HAL_OK)
        {
            transfer_errors++;
            LOG_E("EPD frame start failed at %u", frame_idx);
            return 1;
        }

        return 0;
    }
    else
    {
        return 1; //Done
    }
}
static void LCDC_SendLineCpltCbk(LCDC_HandleTypeDef *hlcdc)
{
    completed_frames++;

    if (StartFrame(hlcdc, curr_frame++))
    {
        HAL_LCDC_LayerSetFormat(hlcdc, HAL_LCDC_LAYER_DEFAULT, ori_format); //Restore layer format
        drawing = RT_FALSE;
        completed_draws++;
        hlcdc->XferCpltCallback = Ori_XferCpltCallback;
        if (Ori_XferCpltCallback) Ori_XferCpltCallback(hlcdc);
        Ori_XferCpltCallback = NULL;
    }
}

static void LCD_WriteMultiplePixels(LCDC_HandleTypeDef *hlcdc, const uint8_t *RGBCode, uint16_t Xpos0, uint16_t Ypos0, uint16_t Xpos1, uint16_t Ypos1)
{
    if (!panel_power_ready)
    {
        LOG_E("EPD draw skipped: panel power is not ready");
        if (hlcdc->XferCpltCallback) hlcdc->XferCpltCallback(hlcdc);
        return;
    }
    ori_format = hlcdc->Layer[HAL_LCDC_LAYER_DEFAULT].data_format;

    LOG_I("LCD_WriteMultiplePixels %d pixels to %d, %d", (Xpos1 - Xpos0) * (Ypos1 - Ypos0), Xpos0, Ypos0);

    CopyToMixedGrayBuffer(hlcdc, RGBCode, Xpos0, Ypos0, Xpos1, Ypos1);
    mpu_dcache_clean(mixed_framebuffer, sizeof(mixed_framebuffer));
    HAL_LCDC_LayerSetData(hlcdc, HAL_LCDC_LAYER_DEFAULT, (uint8_t *)mixed_framebuffer, 0, 0, EPD_PANEL_HOR - 1, EPD_PANEL_VER - 1);
    HAL_LCDC_LayerSetFormat(hlcdc, HAL_LCDC_LAYER_DEFAULT, LCDC_PIXEL_FORMAT_L8);

    HAL_LCDC_LayerSetLTab(hlcdc, HAL_LCDC_LAYER_DEFAULT, (LCDC_AColorDef *)lut);


    total_frames = epd_wave_table_get_frames();
    curr_frame = 1; /* Frame zero is started below, once. */
    LOG_I("Done. Start to flush total_frames = %d", total_frames);


    if (!total_frames)
    {
        LOG_E("EPD draw skipped: empty waveform");
        HAL_LCDC_LayerSetFormat(hlcdc, HAL_LCDC_LAYER_DEFAULT, ori_format);
        if (hlcdc->XferCpltCallback) hlcdc->XferCpltCallback(hlcdc);
        return;
    }
    Ori_XferCpltCallback = hlcdc->XferCpltCallback;
    hlcdc->XferCpltCallback = LCDC_SendLineCpltCbk;
    drawing = RT_TRUE;
    if (StartFrame(hlcdc, 0))
    {
        drawing = RT_FALSE;
        HAL_LCDC_LayerSetFormat(hlcdc, HAL_LCDC_LAYER_DEFAULT, ori_format);
        hlcdc->XferCpltCallback = Ori_XferCpltCallback;
        if (Ori_XferCpltCallback) Ori_XferCpltCallback(hlcdc);
        Ori_XferCpltCallback = NULL;
        return;
    }
}

static void LCD_WriteReg(LCDC_HandleTypeDef *hlcdc, uint16_t LCD_Reg, uint8_t *Parameters, uint32_t NbParameters)
{
    HAL_LCDC_WriteU8Reg(hlcdc, LCD_Reg, Parameters, NbParameters);
}


/**
  * @brief  Reads the selected LCD Register.
  * @param  RegValue: Address of the register to read
  * @param  ReadSize: Number of bytes to read
  * @retval LCD Register Value.
  */
static uint32_t LCD_ReadData(LCDC_HandleTypeDef *hlcdc, uint16_t RegValue, uint8_t ReadSize)
{
    uint32_t rd_data = 0;

    HAL_LCDC_ReadU8Reg(hlcdc, RegValue, (uint8_t *)&rd_data, ReadSize);
    return rd_data;
}



static uint32_t LCD_ReadPixel(LCDC_HandleTypeDef *hlcdc, uint16_t Xpos, uint16_t Ypos)
{
    return 0;
}


static void LCD_SetColorMode(LCDC_HandleTypeDef *hlcdc, uint16_t color_mode)
{
}

static void LCD_SetBrightness(LCDC_HandleTypeDef *hlcdc, uint8_t br)
{

}

static void IdleModeOn(LCDC_HandleTypeDef *hlcdc)
{
#ifdef BSP_USING_BOARD_SF32LB57GY_OED_E0470A03
    /* Panel spec section 6.1: OE low >=12 us before VPOS/VNEG off. */
    HAL_PIN_Set(PAD_PA26, GPIO_A26, PIN_NOPULL, 1);
    BSP_GPIO_Set(26, 0, 1);
    BSP_LCD_GMODE_Set(0);
    HAL_Delay_us(20);
#endif
    tps_enter_sleep();
#ifdef BSP_USING_BOARD_SF32LB57GY_OED_E0470A03
    extern void BSP_EPD_BusIdle(void);
    BSP_EPD_BusIdle();
#endif
    panel_power_ready = RT_FALSE;

}

static void IdleModeOff(LCDC_HandleTypeDef *hlcdc)
{
#ifdef BSP_USING_BOARD_SF32LB57GY_OED_E0470A03
    BSP_LCD_PowerUp();
    GPIO_InitTypeDef oe = {.Pin=26, .Mode=GPIO_MODE_INPUT, .Pull=GPIO_NOPULL};
    HAL_GPIO_Init(hwp_gpio1, &oe); /* Release GPIO override before TCON owns OE. */
    HAL_PIN_Set(PAD_PA26, LCDC1_TCON_SDOE, PIN_NOPULL, 1);
#endif
    panel_power_ready = (tps_exit_sleep() == RT_EOK);
}

static const LCD_DrvOpsDef lcd_drv_operations =
{
    LCD_Init,
    LCD_ReadID,
    LCD_DisplayOn,
    LCD_DisplayOff,

    LCD_SetRegion,
    LCD_WritePixel,
    LCD_WriteMultiplePixels,
    NULL,

    LCD_SetColorMode,
    LCD_SetBrightness,
    IdleModeOn,
    IdleModeOff,
    NULL,
    NULL,
    NULL,
    NULL
};

LCD_DRIVER_EXPORT2(epd_e0470a03_57, THE_LCD_ID, &lcdc_int_cfg,
                   &lcd_drv_operations, 1);


void epd_get_power_status(epd_power_status_t *s)
{
    s->draws = completed_draws;
    s->frames = completed_frames;
    s->errors = transfer_errors;
    s->busy = drawing;
    s->power_ready = panel_power_ready;
}

#ifdef EPD_POWER_DIAGNOSTICS
uint32_t epd_framebuffer_checksum(void)
{
    uint32_t hash = 2166136261u;
    unsigned i;
    /* The draw path cleaned this buffer. Bypass old cache lines on wake. */
    mpu_dcache_invalidate(mixed_framebuffer, sizeof(mixed_framebuffer));
    for (i=0; i<sizeof(mixed_framebuffer); ++i)
        hash = (hash ^ mixed_framebuffer[i]) * 16777619u;
    return hash;
}
#endif
