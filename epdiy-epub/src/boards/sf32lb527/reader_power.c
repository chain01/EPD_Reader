/* Idle deep sleep retains the reader and PSRAM; touch wakes the same page. */
#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <finsh.h>
#include <string.h>
#include "bf0_hal.h"
#include "bf0_pm.h"
#include "reader_power.h"

#ifdef RT_USING_PM
static uint32_t entered, exits, slept_ms, longest_ms, start_ms;
static int forced_awake;
static struct rt_timer test_timer;

static void diagnostic_hold(int hold)
{
    rt_base_t level=rt_hw_interrupt_disable();
    if (hold && !forced_awake) { reader_power_busy(); forced_awake=1; }
    if (!hold && forced_awake) { forced_awake=0; reader_power_idle(); }
    rt_hw_interrupt_enable(level);
}

static void test_finished(void *arg)
{
    (void)arg;
    /* Timed test returns to awake so a non-wake-capable UART can read results. */
    diagnostic_hold(1);
}

static void notify(uint8_t event, uint8_t mode, void *data)
{
    (void)data;
    if (mode != PM_SLEEP_MODE_DEEP) return;
    if (event == RT_PM_ENTER_SLEEP) {
        ++entered;
        start_ms = rt_tick_get();
    } else {
        uint32_t elapsed = rt_tick_get()-start_ms;
        ++exits;
        slept_ms += elapsed;
        if (elapsed > longest_ms) longest_ms = elapsed;
    }
}

static int pwr(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "off")) {
        rt_timer_stop(&test_timer); diagnostic_hold(1);
    } else if (argc == 2 && !strcmp(argv[1], "on")) {
        rt_timer_stop(&test_timer); diagnostic_hold(0);
    } else if (argc == 2 && !strcmp(argv[1], "test")) {
        rt_timer_start(&test_timer); diagnostic_hold(0);
    }
    rt_kprintf("PWR_STATUS auto=%d uptime_ms=%u deep_enter=%u deep_exit=%u slept_ms=%u longest_ms=%u touch_irq=PA44 wake_pin=20\n",
        !forced_awake, rt_tick_get(), entered, exits, slept_ms, longest_ms);
    return 0;
}
MSH_CMD_EXPORT(pwr, Idle deep sleep: status / on / off / test [10 seconds then awake]);

int board527_power_init(void)
{
    /* PA44 is HPAON PIN20 on SF32LB52x. Keep its GPIO IRQ enabled as well. */
    int rc = pm_enable_pin_wakeup(20, AON_PIN_MODE_NEG_EDGE);
    if (rc) return rc;
    rt_timer_init(&test_timer,"pwrtest",test_finished,RT_NULL,
                  rt_tick_from_millisecond(10000),RT_TIMER_FLAG_ONE_SHOT);
    rt_pm_notify_set(notify, RT_NULL);
    rt_kprintf("PWR_READY policy=deep tickless=1 touch_wake=PA44 rails_retained=1\n");
    return 0;
}
#else
int board527_power_init(void) { return 0; }
#endif
