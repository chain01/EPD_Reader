/* SPDX-License-Identifier: Apache-2.0 */
#include <rtthread.h>
#include "board527.h"
#include "frontlight.h"
int board527_init(void)
{
    static int initialized;
    if(initialized)return 0;
    if(tps_init(1100)||board527_display_init())return -1;
    int light=frontlight_start(),touch=board527_touch_init();
    initialized=1;
    rt_kprintf("BOARD527_READY display=1 touch=%d frontlight=%d PSRAM=8MiB NOR=16MiB\n",!touch,!light);
    return touch||light?-1:0;
}
