/* SPDX-License-Identifier: Apache-2.0
 * SF32LB527 / OED v1.2 / E0470A03-AF-S. DBI shifts data; GPIO scans gates.
 * Boot leaves panel HV off. No filesystem, radio, or Flash write test.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "drv_flash.h"
#include "mem_section.h"
#include "mem_map.h"
#include "epd_tps.h"
#include "epd_waveform_bin_reader.h"
#define WAVE_BYTES 223240u
#define WAVE_FNV 0x1a2e9bf7u
#include "patterns.h"
#include "board527.h"
#include "frontlight.h"
#include "tf_card.h"

#define W 1216
#define H 684
#define DATA_BYTES (W / 4)
#define ROW_BYTES (DATA_BYTES + 12)
#define CACHE_BYTES (2u * 1024 * 1024)
L2_NON_RET_BSS_SECT_BEGIN(epd_test)
L2_NON_RET_BSS_SECT(epd_test, ALIGN(64) static uint8_t mixed[W * H]);
L2_NON_RET_BSS_SECT(epd_test, ALIGN(64) static uint8_t reader_frame[W * H / 2]);
L2_NON_RET_BSS_SECT(epd_test, ALIGN(64) static uint8_t scan_cache[CACHE_BYTES]);
L2_NON_RET_BSS_SECT_END
static LCDC_HandleTypeDef lcd;
static uint32_t lut[256];
static struct rt_mutex lock;
static struct rt_timer cutoff;
static volatile int aborted;
/* COM34 optical comparison: gatepol=0 displays normally; gatepol=1 failed. */
static int ready, previous_known, gatepol = 0, temperature = 28;
static unsigned chunk_start,chunk_rows,full_period=10,refresh_index;
static int vcom_mv = 1100;
static int lcd_ready;
static unsigned draws, frames_total, errors, last_frames;
static uint32_t last_ms;
static unsigned source_mhz=48, gate_us=1, unique_rows;
static unsigned packed_dummy=1;
static unsigned pipeline=1;
ALIGN(32) static uint8_t neutral_row[ROW_BYTES];
static uint16_t row_map[H], unique_y[H];
static uint32_t row_hash[H];
static const uint8_t *scan_source;
static int use_cache=1;
static uint32_t last_convert_us,last_scan_us;

L1_RET_CODE_SECT(epd527, static void pin(unsigned n, unsigned v))
{
    GPIO1_TypeDef *gpio = (GPIO1_TypeDef *)hwp_gpio1;
    if (n < 32) {
        if (v) gpio->DOSR0 = 1u << n;
        else gpio->DOCR0 = 1u << n;
    } else {
        if (v) gpio->DOSR1 = 1u << (n - 32);
        else gpio->DOCR1 = 1u << (n - 32);
    }
}
L1_RET_CODE_SECT(epd527, static void gate(int active)) { pin(EPD_CPV, active ^ gatepol); }
static void idle(void)
{
    pin(EPD_OE, 0);
    pin(EPD_LE, 0);
    pin(EPD_GMODE, 0);
    pin(EPD_STV, 1);
    gate(0);
}
static void power_cutoff(void *arg)
{
    (void)arg;
    aborted = 1;
    pin(EPD_OE, 0);
    HAL_Delay_us(20);
    pin(3, 0); /* AW reset releases GE6 to SY7636 EN pulldown. No I2C in ISR. */
    pin(EPD_GMODE, 0);
}
void *waveform_bin_reader_malloc(size_t n) { return rt_malloc(n); }
void waveform_bin_reader_free(void *p) { rt_free(p); }
int waveform_bin_reader_read_data(uint32_t off, uint8_t *buf, uint32_t size)
{
    if (off > WAVE_BYTES || size > WAVE_BYTES - off) return -1;
    return rt_flash_read(CUSTOM_EPD_WAVE_TABLE_START_ADDR + off, buf, size);
}

static int setup_lcd(void)
{
    unsigned transfer_bytes=DATA_BYTES+(packed_dummy?12:0);
    memset(&lcd, 0, sizeof(lcd));
    lcd.Instance = hwp_lcdc1;
    lcd.Init.lcd_itf = LCDC_INTF_DBI_8BIT_B;
    lcd.Init.freq = source_mhz * 1000000;
    lcd.Init.color_mode = LCDC_PIXEL_FORMAT_RGB332;
    lcd.Init.cfg.dbi.syn_mode = HAL_LCDC_SYNC_DISABLE;
    HAL_RCC_EnableModule(RCC_MOD_LCDC1);
    if (HAL_LCDC_Init(&lcd) != HAL_OK) return -1;
    /* Same raw RGB332 byte transport as the original OED v1.2 driver. */
    lcd.Instance->LAYER0_CONFIG =
        (4u << LCD_IF_LAYER0_CONFIG_FORMAT_Pos) |
        (1u << LCD_IF_LAYER0_CONFIG_ALPHA_SEL_Pos) |
        (255u << LCD_IF_LAYER0_CONFIG_ALPHA_Pos) |
        (transfer_bytes << LCD_IF_LAYER0_CONFIG_WIDTH_Pos) |
        (1u << LCD_IF_LAYER0_CONFIG_ACTIVE_Pos);
    lcd.Instance->LAYER0_TL_POS = 0;
    lcd.Instance->LAYER0_BR_POS = (transfer_bytes - 1) << LCD_IF_LAYER0_BR_POS_X1_Pos;
    lcd.Instance->CANVAS_TL_POS = 0;
    lcd.Instance->CANVAS_BR_POS = (transfer_bytes - 1) << LCD_IF_CANVAS_BR_POS_X1_Pos;
    lcd.Instance->LCD_WR = 0;
    return 0;
}

L1_RET_CODE_SECT(epd527, static int wait_idle(int single))
{
    uint32_t start = HAL_DBG_DWT_GetCycles();
    while (single ? (lcd.Instance->LCD_SINGLE & LCD_IF_LCD_SINGLE_LCD_BUSY) :
                    (lcd.Instance->STATUS & LCD_IF_STATUS_LCD_BUSY)) {
        if (aborted || HAL_DBG_DWT_GetCycles() - start > 2400000u) return -1;
    }
    return aborted ? -1 : 0;
}
L1_RET_CODE_SECT(epd527, static int clocks(unsigned count, int keep_stl_high))
{
    while (count--) {
        lcd.Instance->LCD_SINGLE = LCD_IF_LCD_SINGLE_WR_TRIG |
            (keep_stl_high ? LCD_IF_LCD_SINGLE_TYPE_Msk : 0);
        if (wait_idle(1)) return -1;
    }
    return 0;
}
L1_RET_CODE_SECT(epd527, static void gate_pulse(void))
{
    gate(1); HAL_Delay_us(gate_us);
    gate(0); HAL_Delay_us(gate_us);
}
L1_RET_CODE_SECT(epd527, static const uint8_t *row_source(unsigned y))
{
    unsigned row=row_map[y]-chunk_start;
    return row<chunk_rows?scan_source+row*ROW_BYTES:neutral_row;
}
L1_RET_CODE_SECT(epd527, __attribute__((noinline)) static int scan_frame(void))
{
    pin(EPD_OE, 0);
    pin(EPD_GMODE, 0); HAL_Delay_us(6);
    pin(EPD_GMODE, 1); HAL_Delay_us(6);
    pin(EPD_STV, 0); HAL_Delay_us(6);
    gate(1); HAL_Delay_us(6);
    pin(EPD_STV, 1);
    gate(0); HAL_Delay_us(6);
    for (unsigned i = 0; i < 3; ++i) gate_pulse();

    if(pipeline) {
        /* Preload first source row while OE is low, then latch it. Keep each
         * gate selected while shifting the following row, as the 52X DBI
         * reference does. The final neutral transfer gives the last real row
         * the same selected duration as every other row. */
        if(clocks(1,0))return -1;
        lcd.Instance->LAYER0_SRC=(uint32_t)(row_source(0));
        lcd.Instance->COMMAND=1;
        if(wait_idle(0)||(!packed_dummy&&clocks(12,1)))return -1;
        for(unsigned y=0;y<H;++y) {
            pin(EPD_LE,1);HAL_Delay_us(1);
            pin(EPD_LE,0);HAL_Delay_us(1);
            pin(EPD_OE,1);
            if(clocks(1,0))return -1;
            gate(1);
            lcd.Instance->LAYER0_SRC=(uint32_t)(y+1<H?
                row_source(y+1):neutral_row);
            lcd.Instance->COMMAND=1;
            if(wait_idle(0)||(!packed_dummy&&clocks(12,1)))return -1;
            gate(0);HAL_Delay_us(gate_us);
        }
    } else {
    for (unsigned y = 0; y < H; ++y) {
        if (aborted || clocks(1, 0)) return -1;
        lcd.Instance->LAYER0_SRC = (uint32_t)(row_source(y));
        lcd.Instance->COMMAND = 1;
        if (wait_idle(0) || (!packed_dummy && clocks(12, 1))) return -1;
        /* >=10 dummy source clocks after data, LE >=300 ns,
         * and >=800 ns before next STL, per panel specification. */
        pin(EPD_LE, 1); HAL_Delay_us(1);
        pin(EPD_LE, 0); HAL_Delay_us(1);
        pin(EPD_OE, 1);
        gate_pulse();
    }
    }
    pin(EPD_OE, 0);
    for (unsigned i = 0; i < 5; ++i) gate_pulse();
    return 0;
}


/* Cache all waveform phases for a bounded set of unique rows. If a complex
 * image exceeds 2MiB, remaining rows are refreshed in another pass with neutral
 * data elsewhere. Each pixel retains the validated gate timing and all phases;
 * arbitrary images never fall back to slow conversion while a gate is active. */
static void map_rows(void)
{
    unique_rows=0;
    for(unsigned y=0;y<H;++y) {
        const uint8_t *row=mixed+y*W;uint32_t hash=2166136261u;
        for(unsigned x=0;x<W;++x)hash=(hash^row[x])*16777619u;
        unsigned n;
        for(n=0;n<unique_rows;++n)
            if(row_hash[n]==hash&&!memcmp(row,mixed+unique_y[n]*W,W))break;
        if(n==unique_rows){row_hash[n]=hash;unique_y[n]=y;++unique_rows;}
        row_map[y]=n;
    }
}
static void prepare_chunk(unsigned frames)
{
    for(unsigned f=0;f<frames;++f) {
        waveform_bin_reader_fill_lut(lut,f);
        for(unsigned i=0;i<256;++i)lut[i]=(lut[i]>>3)&3;
        uint8_t *out=scan_cache+f*chunk_rows*ROW_BYTES;
        for(unsigned r=0;r<chunk_rows;++r) {
            const uint8_t *m=mixed+unique_y[chunk_start+r]*W;
            for(unsigned x=0;x<DATA_BYTES;++x)
                out[r*ROW_BYTES+x]=(lut[m[x*4]]<<6)|(lut[m[x*4+1]]<<4)|(lut[m[x*4+2]]<<2)|lut[m[x*4+3]];
            memset(out+r*ROW_BYTES+DATA_BYTES,0,12);
        }
    }
    mpu_dcache_clean(scan_cache,frames*chunk_rows*ROW_BYTES);
}
static int drive(unsigned mode)
{
    unsigned count=waveform_bin_reader_get_frames(temperature,(WAVE_TABLE_MODE_T)mode);
    if(!count||count>256)return -1;
    map_rows();unsigned rows_per_chunk=CACHE_BYTES/(count*ROW_BYTES),passes=0;
    uint32_t begin=rt_tick_get(),scan_cycles=0;unsigned done=0;
    for(chunk_start=0;chunk_start<unique_rows;chunk_start+=chunk_rows) {
        chunk_rows=unique_rows-chunk_start;
        if(chunk_rows>rows_per_chunk)chunk_rows=rows_per_chunk;
        prepare_chunk(count);++passes;aborted=0;
        rt_timer_start(&cutoff);
        if(tps_exit_sleep()!=RT_EOK){aborted=1;goto cleanup;}
        for(unsigned f=0;f<count;++f) {
            scan_source=scan_cache+f*chunk_rows*ROW_BYTES;
            uint32_t tick=HAL_DBG_DWT_GetCycles();
            rt_enter_critical();int rc=scan_frame();rt_exit_critical();
            scan_cycles+=HAL_DBG_DWT_GetCycles()-tick;
            if(rc){aborted=1;break;}++done;
        }
cleanup:
        idle();HAL_Delay_us(20);
        int power_rc=tps_enter_sleep();rt_timer_stop(&cutoff);
        if(aborted||power_rc){++errors;previous_known=0;return -1;}
    }
    ++draws;frames_total+=done;last_frames=done;last_ms=rt_tick_get()-begin;
    last_scan_us=done?scan_cycles/240/done:0;
    rt_kprintf("EPD527_REFRESH mode=%u frames=%u rows=%u passes=%u ms=%u scan_us=%u errors=%u HV=OFF\n",
        mode,done,unique_rows,passes,last_ms,last_scan_us,errors);
    return 0;
}
static void lock_devices(void)
{
    tf_card_display_lock();frontlight_display_lock();board527_touch_lock();
}
static void unlock_devices(void)
{
    board527_touch_unlock();frontlight_display_unlock();tf_card_display_unlock();
}
int board527_display_present(const uint8_t *gray4)
{
    if(!ready||!gray4)return -1;
    rt_mutex_take(&lock,RT_WAITING_FOREVER);lock_devices();
    int rc=0,has_gray=0,old_gray=0;
    if(!previous_known) {
        memset(mixed,0xff,sizeof(mixed));
        rc=drive(3);if(rc)goto finish;
        previous_known=1;
    }
    for(unsigned i=0;i<W*H;++i) {
        uint8_t old=mixed[i]&15,new_value=(gray4[i/2]>>((i&1)*4))&15;
        has_gray|=new_value!=0&&new_value!=15;old_gray|=old!=0&&old!=15;
        mixed[i]=(old<<4)|new_value;
    }
    unsigned mode=(!has_gray&&!old_gray&&full_period&&refresh_index%full_period)?1:0;
    rc=drive(mode);if(!rc){previous_known=1;++refresh_index;}
finish:
    unlock_devices();rt_mutex_release(&lock);return rc;
}
int board527_display_init(void)
{
    uint32_t hash=2166136261u;const uint8_t *wave=(const uint8_t *)CUSTOM_EPD_WAVE_TABLE_START_ADDR;
    HAL_DBG_DWT_Init();rt_mutex_init(&lock,"epd527",RT_IPC_FLAG_PRIO);
    rt_timer_init(&cutoff,"epdlimit",power_cutoff,RT_NULL,rt_tick_from_millisecond(8000),RT_TIMER_FLAG_ONE_SHOT);
    idle();
    for(unsigned i=0;i<WAVE_BYTES;++i)hash=(hash^wave[i])*16777619u;
    if(hash!=WAVE_FNV){rt_kprintf("EPD527_WAVE_MISMATCH %08x\n",hash);return -1;}
    lcd_ready=waveform_bin_reader_init(CUSTOM_EPD_WAVE_TABLE_SIZE)==0&&setup_lcd()==0;
    ready=lcd_ready;previous_known=0;memset(mixed,0xff,sizeof(mixed));
    rt_kprintf("EPD527_READY=%d native=1216x684 VCOM=-1100mV source=48MHz row_cache=2MiB\n",ready);
    return ready?0:-1;
}
void board527_display_off(void){idle();}
uint8_t *board527_framebuffer(void){return reader_frame;}
void set_part_disp_times(int count){full_period=count<0?0:(unsigned)count;refresh_index=0;}
static int epd(int argc,char **argv)
{
    (void)argc;(void)argv;
    rt_kprintf("EPD527_STATUS ready=%d known=%d draws=%u frames=%u errors=%u last_ms=%u passes_use_neutral_rows=1\n",ready,previous_known,draws,frames_total,errors,last_ms);
    return 0;
}
MSH_CMD_EXPORT(epd, E0470A03 status);
