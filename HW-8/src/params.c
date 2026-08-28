#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/printk.h>

#include "kernel_pc.h"

static int parse_uint_param(const char *val, unsigned int *value)
{
	return kstrtouint(val, 0, value);
}

static int set_fifo_size(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (!pc_fifo_size_valid(value))
		return PC_INVALID;

	pc_context.fifo_size = value;
	return PC_OK;
}

static int get_fifo_size(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n", pc_context.fifo_size);
}

static const struct kernel_param_ops fifo_size_ops = {
	.set = set_fifo_size,
	.get = get_fifo_size,
};

module_param_cb(fifo_size, &fifo_size_ops, NULL, 0444);
MODULE_PARM_DESC(fifo_size, "kfifo slots, power of two, 4..1024");

static int set_num_events(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (!pc_num_events_valid(value))
		return PC_INVALID;

	pc_context.num_events = value;
	return PC_OK;
}

static int get_num_events(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n", pc_context.num_events);
}

static const struct kernel_param_ops num_events_ops = {
	.set = set_num_events,
	.get = get_num_events,
};

module_param_cb(num_events, &num_events_ops, NULL, 0444);
MODULE_PARM_DESC(num_events, "Number of producer events, 1..50000");

static int set_interval_us(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (!pc_interval_us_valid(value))
		return PC_INVALID;

	pc_context.interval_us = value;
	return PC_OK;
}

static int get_interval_us(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n", pc_context.interval_us);
}

static const struct kernel_param_ops interval_us_ops = {
	.set = set_interval_us,
	.get = get_interval_us,
};

module_param_cb(interval_us, &interval_us_ops, NULL, 0444);
MODULE_PARM_DESC(interval_us, "Producer interval in microseconds, 100..1000000");

static int set_consumer_type(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (!pc_consumer_type_valid(value))
		return PC_INVALID;

	if (!pc_module_ready) {
		pc_context.consumer_type = value;
		return PC_OK;
	}

	if (!mutex_trylock(&pc_control_mutex))
		return PC_BUSY;

	pc_context.consumer_type = value;
	mutex_unlock(&pc_control_mutex);

	return PC_OK;
}

static int get_consumer_type(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u (%s)\n",
			 pc_context.consumer_type,
			 pc_consumer_name(pc_context.consumer_type));
}

static const struct kernel_param_ops consumer_type_ops = {
	.set = set_consumer_type,
	.get = get_consumer_type,
};

module_param_cb(consumer_type, &consumer_type_ops, NULL, 0644);
MODULE_PARM_DESC(consumer_type, "Consumer type: 0 tasklet, 1 workqueue");

static int set_run(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (value != 1)
		return PC_INVALID;

	ret = pc_run_test(&pc_context);
	if (ret)
		pr_err("run failed: %d\n", ret);

	return ret;
}

static const struct kernel_param_ops run_ops = {
	.set = set_run,
};

module_param_cb(run, &run_ops, NULL, 0200);
MODULE_PARM_DESC(run, "Write 1 to run producer consumer test");

static int get_result(char *buffer, const struct kernel_param *kp)
{
	const char *consumer;
	int produced;
	int consumed;
	int dropped;
	int lost;
	int len;

	produced = atomic_read(&pc_context.produced);
	consumed = atomic_read(&pc_context.consumed);
	dropped = atomic_read(&pc_context.dropped);
	lost = produced > consumed ? produced - consumed : 0;
	consumer = pc_consumer_name(pc_context.last_run_consumer_type);

	if (pc_context.last_run_result == PC_OK)
		len = scnprintf(buffer, PAGE_SIZE,
				"produced=%d consumed=%d dropped=%d consumer=%s ok",
				produced, consumed, dropped, consumer);
	else
		len = scnprintf(buffer, PAGE_SIZE,
				"produced=%d consumed=%d dropped=%d consumer=%s error=%d",
				produced, consumed, dropped, consumer,
				pc_context.last_run_result);

	if (lost)
		len += scnprintf(buffer + len, PAGE_SIZE - len,
				 " warn: lost=%d", lost);

	len += scnprintf(buffer + len, PAGE_SIZE - len, "\n");
	return len;
}

static const struct kernel_param_ops result_ops = {
	.get = get_result,
};

module_param_cb(result, &result_ops, NULL, 0444);
MODULE_PARM_DESC(result, "Last producer consumer test result");

static int get_stats(char *buffer, const struct kernel_param *kp)
{
	unsigned int last;
	int produced;
	int consumed;
	int dropped;
	u64 sum;
	u64 avg;

	produced = atomic_read(&pc_context.produced);
	consumed = atomic_read(&pc_context.consumed);
	dropped = atomic_read(&pc_context.dropped);

	if (pc_context.last_run_consumer_type == PC_CONSUMER_WORKQUEUE) {
		mutex_lock(&pc_context.stats_lock);
		sum = pc_context.sum;
		last = pc_context.last_value;
		mutex_unlock(&pc_context.stats_lock);
	} else {
		sum = READ_ONCE(pc_context.sum);
		last = READ_ONCE(pc_context.last_value);
	}

	avg = consumed ? div64_u64(sum, consumed) : 0;

	return scnprintf(buffer, PAGE_SIZE,
			 "produced=%d consumed=%d dropped=%d sum=%llu last=%u avg=%llu\n",
			 produced, consumed, dropped, (unsigned long long)sum, last,
			 (unsigned long long)avg);
}

static const struct kernel_param_ops stats_ops = {
	.get = get_stats,
};

module_param_cb(stats, &stats_ops, NULL, 0444);
MODULE_PARM_DESC(stats, "Last producer consumer test statistics");

static int set_reset(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (value == 0)
		return PC_INVALID;

	if (!mutex_trylock(&pc_control_mutex))
		return PC_BUSY;

	atomic_set(&pc_context.stopping, 1);
	hrtimer_cancel(&pc_context.timer);
	pc_consumer_stop(&pc_context, false);
	kfifo_reset(&pc_context.fifo);
	pc_clear_run_state(&pc_context);
	pc_context.active = false;
	pc_context.last_run_result = PC_OK;
	pc_context.last_run_consumer_type = pc_context.consumer_type;

	mutex_unlock(&pc_control_mutex);
	return PC_OK;
}

static const struct kernel_param_ops reset_ops = {
	.set = set_reset,
};

module_param_cb(reset, &reset_ops, NULL, 0200);
MODULE_PARM_DESC(reset, "Write non-zero value to reset queue and statistics");
