#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/printk.h>

#include "kernel_sync.h"

static int parse_uint_param(const char *val, unsigned int *value)
{
	return kstrtouint(val, 0, value);
}

static int set_num_threads(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (!sync_num_threads_valid(value))
		return SD_INVALID;

	if (!mutex_trylock(&sync_control_mutex))
		return SD_BUSY;

	sync_context.num_threads = value;
	mutex_unlock(&sync_control_mutex);

	return SD_OK;
}

static int get_num_threads(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n", sync_context.num_threads);
}

static const struct kernel_param_ops num_threads_ops = {
	.set = set_num_threads,
	.get = get_num_threads,
};

module_param_cb(num_threads, &num_threads_ops, NULL, 0644);
MODULE_PARM_DESC(num_threads, "Number of worker kthreads, 1..32");

static int set_iterations(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (!sync_iterations_valid(value))
		return SD_INVALID;

	if (!mutex_trylock(&sync_control_mutex))
		return SD_BUSY;

	sync_context.iterations = value;
	mutex_unlock(&sync_control_mutex);

	return SD_OK;
}

static int get_iterations(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n", sync_context.iterations);
}

static const struct kernel_param_ops iterations_ops = {
	.set = set_iterations,
	.get = get_iterations,
};

module_param_cb(iterations, &iterations_ops, NULL, 0644);
MODULE_PARM_DESC(iterations, "Iterations per thread, 1..1000000");

static int set_lock_type(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (!sync_lock_type_valid(value))
		return SD_INVALID;

	if (!mutex_trylock(&sync_control_mutex))
		return SD_BUSY;

	sync_context.lock_type = value;
	mutex_unlock(&sync_control_mutex);

	return SD_OK;
}

static int get_lock_type(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u (%s)\n", sync_context.lock_type,
			 sync_lock_name(sync_context.lock_type));
}

static const struct kernel_param_ops lock_type_ops = {
	.set = set_lock_type,
	.get = get_lock_type,
};

module_param_cb(lock_type, &lock_type_ops, NULL, 0644);
MODULE_PARM_DESC(lock_type, "Lock type: 0 spinlock, 1 mutex, 2 semaphore");

static int set_run(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (value != 1)
		return SD_INVALID;

	ret = sync_run_test(&sync_context);
	if (ret)
		pr_err("run failed: %d\n", ret);

	return ret;
}

static const struct kernel_param_ops run_ops = {
	.set = set_run,
};

module_param_cb(run, &run_ops, NULL, 0200);
MODULE_PARM_DESC(run, "Write 1 to run synchronization test");

static int get_result(char *buffer, const struct kernel_param *kp)
{
	const char *status;

	status = sync_context.last_run_result == SD_OK &&
			 sync_context.shared_counter == 0 ?
			 "ok" :
			 "failed";

	if (sync_context.last_run_result == SD_OK)
		return scnprintf(buffer, PAGE_SIZE,
				 "counter=%lld threads=%u iterations=%u lock=%s %s\n",
				 sync_context.shared_counter, sync_context.num_threads,
				 sync_context.iterations,
				 sync_lock_name(sync_context.lock_type), status);

	return scnprintf(buffer, PAGE_SIZE,
			 "counter=%lld threads=%u iterations=%u lock=%s %s error=%d\n",
			 sync_context.shared_counter, sync_context.num_threads,
			 sync_context.iterations, sync_lock_name(sync_context.lock_type),
			 status, sync_context.last_run_result);
}

static const struct kernel_param_ops result_ops = {
	.get = get_result,
};

module_param_cb(result, &result_ops, NULL, 0444);
MODULE_PARM_DESC(result, "Last run result");

static int get_stats(char *buffer, const struct kernel_param *kp)
{
	s64 total_wait_ns;
	s64 avg_wait_ns = 0;

	total_wait_ns = ktime_to_ns(sync_context.total_wait_time);
	if (sync_context.contention_count != 0)
		avg_wait_ns = div_s64(total_wait_ns,
					 sync_context.contention_count);

	return scnprintf(buffer, PAGE_SIZE,
			 "contention=%u total_wait_ns=%lld avg_wait_ns=%lld\n",
			 sync_context.contention_count, total_wait_ns, avg_wait_ns);
}

static const struct kernel_param_ops stats_ops = {
	.get = get_stats,
};

module_param_cb(stats, &stats_ops, NULL, 0444);
MODULE_PARM_DESC(stats, "Last run wait statistics");

static int set_reset(const char *val, const struct kernel_param *kp)
{
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (value == 0)
		return SD_INVALID;

	if (!mutex_trylock(&sync_control_mutex))
		return SD_BUSY;

	sync_context.shared_counter = 0;
	sync_context.total_wait_time = 0;
	sync_context.contention_count = 0;
	sync_context.last_run_result = SD_OK;
	atomic_set(&sync_context.threads_done, 0);

	mutex_unlock(&sync_control_mutex);

	return SD_OK;
}

static const struct kernel_param_ops reset_ops = {
	.set = set_reset,
};

module_param_cb(reset, &reset_ops, NULL, 0200);
MODULE_PARM_DESC(reset, "Write non-zero value to reset state");
