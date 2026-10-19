/* SPDX-License-Identifier: Apache-2.0
 * SF32LB527 / OED v1.2 / E0470A03-AF-S. DBI shifts data; GPIO scans gates.
 * Updates track old/new gray levels and leave panel HV off after scanning.
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
#include "board527.h"
#include "frontlight.h"
#include "tf_card.h"
#include "epub_mem.h"

#define W 1216
#define H 684
#define DATA_BYTES (W / 4)
#define ROW_BYTES (DATA_BYTES + 12)
#define CACHE_BYTES (3u * 1024 * 1024)
#define MAX_PHASES 64
#define MAX_ROWS 32768
#define HASH_SLOTS 65536
/* Preparation and scanning never overlap. Reuse the expansion buffer for
 * exact per-row waveform equivalence checks, without increasing PSRAM use. */
typedef union {
    uint8_t frame[H * ROW_BYTES];
    struct {
        uint32_t hashes[MAX_PHASES][H];
        uint32_t masks[H][8];
        uint32_t planes[MAX_PHASES][16];
    } prep;
} PhaseWorkspace;
L2_NON_RET_BSS_SECT_BEGIN(epd_test)
L2_NON_RET_BSS_SECT(epd_test, ALIGN(64) static uint8_t mixed[W * H]);
L2_NON_RET_BSS_SECT(epd_test, ALIGN(64) static uint8_t reader_frame[W * H / 2]);
L2_NON_RET_BSS_SECT(epd_test, ALIGN(64) static uint8_t scan_cache[CACHE_BYTES]);
L2_NON_RET_BSS_SECT(epd_test, ALIGN(64) static PhaseWorkspace workspace);
L2_NON_RET_BSS_SECT(epd_test, static uint16_t phase_rows[MAX_PHASES][H]);
L2_NON_RET_BSS_SECT(epd_test, static uint32_t row_offsets[MAX_ROWS]);
L2_NON_RET_BSS_SECT(epd_test, static uint16_t row_slots[HASH_SLOTS]);
L2_NON_RET_BSS_SECT(epd_test, static uint8_t phase_luts[MAX_PHASES][256]);
L2_NON_RET_BSS_SECT_END
#define full_frame workspace.frame
static LCDC_HandleTypeDef lcd;
static uint32_t lut[256];
static struct rt_mutex lock;
static struct rt_timer cutoff;
static volatile int aborted;
/* COM34 optical comparison: gatepol=0 displays normally; gatepol=1 failed. */
static int ready, previous_known, gatepol = 0, temperature = 28;
static unsigned full_period=10,refresh_index,cached_rows,cache_used,active_phase,raw_cache;
static unsigned packed_rows,reused_rows;
static void *extra_allocation;
static uint8_t *extra_cache;
static unsigned extra_bytes;
static uint8_t *raw_row(unsigned row);
static int lcd_ready;
static unsigned draws, frames_total, errors, last_frames;
static uint32_t last_ms;
static unsigned source_mhz=48, gate_us=1, unique_rows;
static unsigned packed_dummy=1;
static unsigned pipeline=1;
ALIGN(32) static uint8_t neutral_row[ROW_BYTES];
static uint16_t row_map[H], unique_y[H];
static uint32_t row_hash[H];
static uint32_t last_scan_us;

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
    return raw_cache?raw_row(phase_rows[active_phase][y]):full_frame+y*ROW_BYTES;
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


/* Every waveform phase is prepared before HV. Identical packed rows share
 * storage across phases; RLE compresses uniform areas. Each full frame is
 * expanded once and scanned over the entire panel, never in visible tiles. */
static int extend_cache(unsigned needed)
{
    if(extra_cache)return needed<=extra_bytes?0:-1;
    /* Borrow only while presenting an already-rendered page. Keep at least
     * 512KiB free for background layout and fonts; never resize live data. */
    unsigned available=epub_mem_available_size();
    for(unsigned size=2u*1024*1024;size>=512u*1024;size-=512u*1024) {
        if(size<needed||size+512u*1024+63>available)continue;
        extra_allocation=epub_mem_malloc(size+63);
        if(!extra_allocation)continue;
        extra_cache=(uint8_t *)(((uintptr_t)extra_allocation+63)&~(uintptr_t)63);
        extra_bytes=size;return 0;
    }
    return -1;
}
static void release_extra_cache(void)
{
    if(extra_allocation)epub_mem_free(extra_allocation);
    extra_allocation=RT_NULL;extra_cache=RT_NULL;extra_bytes=0;
}
static uint8_t *cache_address(unsigned offset)
{
    return offset<CACHE_BYTES?scan_cache+offset:extra_cache+offset-CACHE_BYTES;
}
static uint8_t *raw_row(unsigned row)
{
    unsigned base_rows=CACHE_BYTES/ROW_BYTES;
    return row<base_rows?scan_cache+row*ROW_BYTES:extra_cache+(row-base_rows)*ROW_BYTES;
}
static void map_rows(void)
{
    unique_rows=0;
    memset(workspace.prep.masks,0,sizeof(workspace.prep.masks));
    for(unsigned y=0;y<H;++y) {
        const uint8_t *row=mixed+y*W;uint32_t hash=2166136261u;
        for(unsigned x=0;x<W;++x)hash=(hash^row[x])*16777619u;
        unsigned n;
        for(n=0;n<unique_rows;++n)
            if(row_hash[n]==hash&&!memcmp(row,mixed+unique_y[n]*W,W))break;
        if(n==unique_rows) {
            row_hash[n]=hash;unique_y[n]=y;++unique_rows;
            for(unsigned x=0;x<W;++x)
                workspace.prep.masks[n][row[x]>>5]|=1u<<(row[x]&31);
        }
        row_map[y]=n;
    }
}

static int cache_row(const uint8_t *raw)
{
    uint8_t encoded[DATA_BYTES*2];unsigned size=0;
    for(unsigned i=0;i<DATA_BYTES;) {
        unsigned run=1;
        while(i+run<DATA_BYTES && raw[i+run]==raw[i] && run<255)++run;
        encoded[size++]=run;encoded[size++]=raw[i];i+=run;
    }
    const uint8_t *data=encoded;unsigned tag=size;
    if(size>=DATA_BYTES){data=raw;size=DATA_BYTES;tag=size|0x8000;}
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<size;++i)hash=(hash^data[i])*16777619u;
    unsigned slot=hash&(HASH_SLOTS-1);
    while(row_slots[slot]) {
        unsigned id=row_slots[slot]-1;
        const uint8_t *stored=cache_address(row_offsets[id]);
        unsigned stored_tag=stored[0]|stored[1]<<8;
        if(stored_tag==tag&&!memcmp(stored+2,data,size))return id;
        slot=(slot+1)&(HASH_SLOTS-1);
    }
    if(cached_rows>=MAX_ROWS-1)return -1;
    /* Encoded records must not straddle the two independently allocated banks. */
    if(cache_used<CACHE_BYTES&&size+2>CACHE_BYTES-cache_used)cache_used=CACHE_BYTES;
    if(cache_used+size+2>CACHE_BYTES+extra_bytes &&
       extend_cache(cache_used+size+2-CACHE_BYTES))return -1;
    unsigned id=cached_rows++;row_offsets[id]=cache_used;row_slots[slot]=id+1;
    uint8_t *out=cache_address(cache_used);out[0]=tag;out[1]=tag>>8;
    memcpy(out+2,data,size);cache_used+=size+2;return id;
}
static int prepare_phases(unsigned count)
{
    cached_rows=cache_used=packed_rows=reused_rows=0;
    memset(row_slots,0,sizeof(row_slots));map_rows();
    ALIGN(4) uint8_t row[DATA_BYTES];uint16_t mapping[H];
    uint8_t relevant[256]={0};
    uint32_t all_used[8]={0};
    for(unsigned r=0;r<unique_rows;++r)for(unsigned i=0;i<8;++i)
        all_used[i]|=workspace.prep.masks[r][i];
    for(unsigned i=0;i<256;++i)relevant[i]=(all_used[i>>5]>>(i&31))&1;
    for(unsigned f=0;f<count;++f) {
        waveform_bin_reader_fill_lut(lut,f);
        for(unsigned i=0;i<256;++i)lut[i]=(lut[i]>>3)&3;
        for(unsigned i=0;i<256;++i)phase_luts[f][i]=relevant[i]?lut[i]:0;
        uint32_t *planes=workspace.prep.planes[f];
        memset(planes,0,16*sizeof(*planes));
        for(unsigned i=0;i<256;++i) {
            if(lut[i]&1)planes[i>>5]|=1u<<(i&31);
            if(lut[i]&2)planes[8+(i>>5)]|=1u<<(i&31);
        }
        unsigned prior;
        for(prior=0;prior<f;++prior)if(!memcmp(phase_luts[prior],phase_luts[f],256))break;
        if(prior<f) {
            memcpy(phase_rows[f],phase_rows[prior],sizeof(phase_rows[f]));
            memcpy(workspace.prep.hashes[f],workspace.prep.hashes[prior],unique_rows*sizeof(uint32_t));
            reused_rows+=unique_rows;continue;
        }
        for(unsigned r=0;r<unique_rows;++r) {
            const uint32_t *mask=workspace.prep.masks[r];
            uint32_t hash=2166136261u;
            for(unsigned i=0;i<16;++i)hash=(hash^(planes[i]&mask[i&7]))*16777619u;
            workspace.prep.hashes[f][r]=hash;
            /* Matching hash is only a filter: verify both output bitplanes
             * for every transition that actually occurs in this input row. */
            for(prior=0;prior<f;++prior) {
                if(workspace.prep.hashes[prior][r]!=hash)continue;
                const uint32_t *other=workspace.prep.planes[prior];unsigned i;
                for(i=0;i<8;++i)
                    if(((planes[i]^other[i])|(planes[i+8]^other[i+8]))&mask[i])break;
                if(i==8)break;
            }
            if(prior<f) {
                mapping[r]=phase_rows[prior][unique_y[r]];++reused_rows;continue;
            }
            const uint32_t *m=(const uint32_t *)(mixed+unique_y[r]*W);
            for(unsigned x=0;x<DATA_BYTES;++x) {
                /* Keep the small LUT in internal SRAM. A 64KiB pair table
                 * in PSRAM causes scattered external-memory reads. */
                uint32_t v=m[x];
                row[x]=(lut[v&255]<<6)|(lut[(v>>8)&255]<<4)|
                       (lut[(v>>16)&255]<<2)|lut[v>>24];
            }
            int id=cache_row(row);if(id<0)return -1;mapping[r]=id;++packed_rows;
        }
        for(unsigned y=0;y<H;++y)phase_rows[f][y]=mapping[row_map[y]];
    }
    return 0;
}
static void expand_row(const uint8_t *in,uint8_t *out)
{
    unsigned tag=in[0]|in[1]<<8;in+=2;
    if(tag&0x8000)memcpy(out,in,DATA_BYTES);
    else {
        const uint8_t *end=in+tag;
        while(in<end){unsigned n=*in++,value=*in++;memset(out,value,n);out+=n;}
    }
}
static void promote_raw_cache(void)
{
    unsigned base_rows=CACHE_BYTES/ROW_BYTES;
    if(cached_rows>base_rows)extend_cache((cached_rows-base_rows)*ROW_BYTES);
    raw_cache=cached_rows<=base_rows+extra_bytes/ROW_BYTES;
    if(!raw_cache)return;
    uint8_t encoded[DATA_BYTES+2];
    /* Every encoded record is <=306 bytes, smaller than a 316-byte raw row.
     * Expand backwards; earlier compressed records can never be overwritten. */
    for(unsigned n=cached_rows;n>0;--n) {
        unsigned row=n-1;const uint8_t *in=cache_address(row_offsets[row]);
        unsigned tag=in[0]|in[1]<<8;
        memcpy(encoded,in,(tag&0x7fff)+2);
        expand_row(encoded,raw_row(row));
        memset(raw_row(row)+DATA_BYTES,0,12);
    }
    mpu_dcache_clean(scan_cache,(cached_rows<base_rows?cached_rows:base_rows)*ROW_BYTES);
    if(cached_rows>base_rows)mpu_dcache_clean(extra_cache,(cached_rows-base_rows)*ROW_BYTES);
}
static void expand_phase(unsigned phase)
{
    for(unsigned y=0;y<H;++y) {
        const uint8_t *in=cache_address(row_offsets[phase_rows[phase][y]]);
        expand_row(in,full_frame+y*ROW_BYTES);
    }
    mpu_dcache_clean(full_frame,sizeof(full_frame));
}
static int drive(unsigned mode)
{
    unsigned count=waveform_bin_reader_get_frames(temperature,(WAVE_TABLE_MODE_T)mode);
    if(!count||count>MAX_PHASES)return -1;
    uint32_t begin=rt_tick_get(),scan_cycles=0,expand_cycles=0,visible_ms=0;unsigned done=0;
    if(prepare_phases(count)) {
        ++errors;rt_kprintf("EPD527_CACHE_FULL HV=OFF rows=%u bytes=%u\n",cached_rows,cache_used);
        release_extra_cache();return -RT_ENOMEM;
    }
    promote_raw_cache();unsigned prep_ms=rt_tick_get()-begin;
    memset(full_frame,0,sizeof(full_frame));aborted=0;
    rt_timer_start(&cutoff);
    if(tps_exit_sleep()!=RT_EOK){aborted=1;goto cleanup;}
    for(unsigned f=0;f<count;++f) {
        active_phase=f;
        uint32_t tick=HAL_DBG_DWT_GetCycles();if(!raw_cache)expand_phase(f);
        expand_cycles+=HAL_DBG_DWT_GetCycles()-tick;
        tick=HAL_DBG_DWT_GetCycles();
        rt_enter_critical();int rc=scan_frame();rt_exit_critical();
        scan_cycles+=HAL_DBG_DWT_GetCycles()-tick;
        if(rc){aborted=1;break;}++done;
    }
cleanup:
    visible_ms=rt_tick_get()-begin;
    idle();HAL_Delay_us(20);
    int power_rc=tps_enter_sleep();rt_timer_stop(&cutoff);
    unsigned borrowed=extra_bytes;release_extra_cache();
    if(aborted||power_rc){++errors;previous_known=0;return -1;}
    ++draws;frames_total+=done;last_frames=done;last_ms=rt_tick_get()-begin;
    last_scan_us=done?scan_cycles/240/done:0;
    rt_kprintf("EPD527_REFRESH mode=%u frames=%u ms=%u prep_ms=%u scan_us=%u expand_us=%u errors=%u HV=OFF\n",
        mode,done,last_ms,prep_ms,last_scan_us,done?expand_cycles/240/done:0,errors);
    rt_kprintf("EPD527_CACHE input_rows=%u stored_rows=%u bytes=%u raw=%u full_screen=1\n",unique_rows,cached_rows,cache_used,raw_cache);
    rt_kprintf("EPD527_REUSE packed_rows=%u reused_rows=%u\n",packed_rows,reused_rows);
    rt_kprintf("EPD527_TIMING visible_ms=%u discharge_guard_ms=450\n",visible_ms);
    rt_kprintf("EPD527_MEMORY borrowed=%u released=1\n",borrowed);
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
static unsigned refresh_mode(unsigned period,unsigned index,int has_gray,int old_gray)
{
    if(!period||index%period==0)return 0;
    return has_gray||old_gray?5:1;
}
int board527_display_present(const uint8_t *gray4)
{
    if(!ready||!gray4)return -1;
    rt_mutex_take(&lock,RT_WAITING_FOREVER);lock_devices();
    int rc=0,has_gray=0,old_gray=0,changed=0;
    if(!previous_known) {
        memset(mixed,0xff,sizeof(mixed));
        rc=drive(3);if(rc)goto finish;
        previous_known=1;
    }
    for(unsigned i=0;i<W*H;++i) {
        uint8_t old=mixed[i]&15,new_value=(gray4[i/2]>>((i&1)*4))&15;
        changed|=old!=new_value;
        has_gray|=new_value!=0&&new_value!=15;old_gray|=old!=0&&old!=15;
        mixed[i]=(old<<4)|new_value;
    }
    if(!changed)goto finish;
    /* Antialiased text also contains grays. GL16 handles these ordinary
     * transitions; GC16 is reserved for the configured cleaning interval. */
    unsigned mode=refresh_mode(full_period,refresh_index,has_gray,old_gray);
    rc=drive(mode);
    if(!rc){previous_known=1;++refresh_index;}
    else if(rc==-RT_ENOMEM) {
        /* Preparation failed before HV, so the displayed old image is intact. */
        for(unsigned i=0;i<W*H;++i)mixed[i]>>=4;
    }
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
    rt_kprintf("EPD527_READY=%d native=1216x684 VCOM=-1100mV source=48MHz row_cache=3MiB compressed\n",ready);
    return ready?0:-1;
}
void board527_display_off(void){idle();}
uint8_t *board527_framebuffer(void){return reader_frame;}
void set_part_disp_times(int count){full_period=count<0?0:(unsigned)count;refresh_index=0;}
static int epd(int argc,char **argv)
{
    (void)argc;(void)argv;
    rt_kprintf("EPD527_STATUS ready=%d known=%d draws=%u frames=%u errors=%u last_ms=%u full_screen=1\n",ready,previous_known,draws,frames_total,errors,last_ms);
    return 0;
}
MSH_CMD_EXPORT(epd, E0470A03 status);
