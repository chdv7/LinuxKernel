#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>

#define TIMER_DEFAULT_INTERVAL_MS 30000U
#define TIMER_MIN_INTERVAL_MS 100U
#define TIMER_MAX_INTERVAL_MS 300000U
#define TIMER_DURATION_MS (5U * 60U * 1000U)

static struct timer_list hello_timer;
static unsigned int interval_ms = TIMER_DEFAULT_INTERVAL_MS;
static unsigned long start_jiffies;
static unsigned long stop_jiffies;

static bool timer_interval_valid(unsigned int value)
{
	return value >= TIMER_MIN_INTERVAL_MS &&
	       value <= TIMER_MAX_INTERVAL_MS;
}

static void hello_timer_callback(struct timer_list *timer)
{
	unsigned long interval;
	unsigned long elapsed;
	unsigned long next;
	unsigned int minute;

	if (time_after(timer->expires, stop_jiffies))
		return;

	elapsed = timer->expires - start_jiffies;
	minute = DIV_ROUND_UP(jiffies_to_msecs(elapsed), 60U * 1000U);
	if (minute == 0)
		minute = 1;

	pr_info("min=%u: Hello, timer!\n", minute);

	interval = msecs_to_jiffies(READ_ONCE(interval_ms));
	next = timer->expires + interval;

	if (time_before_eq(next, stop_jiffies))
		mod_timer(timer, next);
}

static int set_interval_ms(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = kstrtouint(val, 0, &value);
	if (ret)
		return ret;

	if (!timer_interval_valid(value))
		return -EINVAL;

	WRITE_ONCE(interval_ms, value);
	return 0;
}

static int get_interval_ms(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n", READ_ONCE(interval_ms));
}

static const struct kernel_param_ops interval_ms_ops = {
	.set = set_interval_ms,
	.get = get_interval_ms,
};

module_param_cb(interval_ms, &interval_ms_ops, NULL, 0644);
MODULE_PARM_DESC(interval_ms, "Timer interval in milliseconds, 100..300000");

static int __init kernel_timer_init(void)
{
	unsigned long first_expiry;

	if (!timer_interval_valid(interval_ms)) {
		pr_err("invalid interval_ms=%u\n", interval_ms);
		return -EINVAL;
	}

	timer_setup(&hello_timer, hello_timer_callback, 0);

	start_jiffies = jiffies;
	stop_jiffies = start_jiffies + msecs_to_jiffies(TIMER_DURATION_MS);
	first_expiry = start_jiffies + msecs_to_jiffies(interval_ms);

	if (time_before_eq(first_expiry, stop_jiffies))
		mod_timer(&hello_timer, first_expiry);

	pr_info("module loaded: interval_ms=%u duration_ms=%u\n",
		interval_ms, TIMER_DURATION_MS);

	return 0;
}

static void __exit kernel_timer_exit(void)
{
	timer_shutdown_sync(&hello_timer);
	pr_info("module unloaded\n");
}

module_init(kernel_timer_init);
module_exit(kernel_timer_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Chuprov");
MODULE_DESCRIPTION("HW-9: timer_list kernel module");
