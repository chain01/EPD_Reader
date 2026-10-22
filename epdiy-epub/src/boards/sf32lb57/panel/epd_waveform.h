/* SPDX-FileCopyrightText: 2026 SiFli Technologies(Nanjing) Co., Ltd
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef READER57_EPD_WAVEFORM_H
#define READER57_EPD_WAVEFORM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Calibrated text/menu composite; not an index in the vendor BIN. */
#define EPD_WAVE_TEXT 16
void epd_wave_table(void);
int epd_wave_prepare_mode(int mode);
uint32_t epd_wave_table_get_frames(void);
uint32_t epd_wave_last_frames(void);
void epd_wave_table_fill_lut(uint32_t *lut, uint32_t frame_num);

#ifdef __cplusplus
}
#endif
#endif
