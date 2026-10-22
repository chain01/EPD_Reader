/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EPD_INK_H
#define EPD_INK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { EPD_INK_FAST_FRAMES=5, EPD_INK_DEFAULT_FRAMES=7 };
typedef struct {
    uint32_t changed_pixels;
    uint32_t drive_frames;
    uint32_t total_frames;
    uint32_t prepare_us;
    uint32_t wave_us;
    uint32_t driver_us;
    int result;
} EpdInkStats;

/* Call from the single UI/render thread, with a binary known-state framebuffer.
 * frames=0 restores the normal waveform path; 1..18 selects black-only pulses.
 * This experimental mode supports white -> black only, never erasing. */
int epd_ink_configure(unsigned int frames);
int epd_ink_region(unsigned int x, unsigned int y, unsigned int w, unsigned int h);
const EpdInkStats *epd_ink_stats(void);
int epd_ink_vendor_check(void);
int epd_ink_power_ready(void);
#ifdef __cplusplus
}
#endif
#endif
