/* SPDX-License-Identifier: Apache-2.0
 * Use the verified E0470A03 binary only, with no other-panel fallback.
 * 28 C is the established bench setting, not a temperature measurement.
 */
#include <rtthread.h>
#include "mem_map.h"
#include "epd_waveform.h"
#include "epd_waveform_bin_reader.h"
static int ready;
static int selected_mode = 3;
static uint32_t frames;
void epd_wave_table(void)
{
    ready = waveform_bin_reader_init(CUSTOM_EPD_WAVE_TABLE_SIZE) == 0;
}
void epd_wave_set_mode(int mode) { selected_mode = mode; }
int epd_wave_get_mode(void) { return selected_mode; }
uint32_t epd_wave_last_frames(void) { return frames; }
uint32_t epd_wave_table_get_frames(int temperature, EpdDrawMode mode)
{
    (void)temperature;
    (void)mode;
    frames = 0;
    if (ready && (selected_mode == 0 || selected_mode == 1 || selected_mode == 3 || selected_mode == 5))
        frames = waveform_bin_reader_get_frames(28, (WAVE_TABLE_MODE_T)selected_mode);
    return frames;
}
void epd_wave_table_fill_lut(uint32_t *lut, uint32_t frame_num)
{
    unsigned i;
    waveform_bin_reader_fill_lut(lut, frame_num);
    for (i = 0; i < 256; ++i)
    {
        /* Vendor bits 4:3 -> EPD_8BIT/F2_SWAP bits 1:0. */
        lut[i] = (lut[i] >> 3) | 0xff000000u;
        /* DU leaves unchanged pixels neutral. The controller still scans
         * the full panel; this is not shortened rectangular scanning. */
        if (selected_mode == 1 && (i >> 4) == (i & 15))
            lut[i] = 0xff000000u;
    }
}
