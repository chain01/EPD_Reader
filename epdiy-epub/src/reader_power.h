#pragma once
#include <rtthread.h>
#include <rtdevice.h>

/* A task may block on DMA, storage or another worker without being idle work.
 * Keep clocks/memories available until the complete transaction has finished. */
static inline void reader_power_busy(void)
{
#if defined(READER_E0470A03) && defined(RT_USING_PM)
    rt_pm_request(PM_SLEEP_MODE_IDLE);
#endif
}
static inline void reader_power_idle(void)
{
#if defined(READER_E0470A03) && defined(RT_USING_PM)
    rt_pm_release(PM_SLEEP_MODE_IDLE);
#endif
}
