/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EPD_POWER_STATUS_H
#define EPD_POWER_STATUS_H
#include <stdint.h>
typedef struct
{
    uint32_t draws, frames, errors;
    int busy, power_ready;
} epd_power_status_t;
void epd_get_power_status(epd_power_status_t *status);
#endif
