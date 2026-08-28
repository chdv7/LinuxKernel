#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>

#include "kernel_sync.h"

struct sync_ctx sync_context = {
	.num_threads = SYNC_DEFAULT_NUM_THREADS,
	.iterations = SYNC_DEFAULT_ITERATIONS,
	.lock_type = SYNC_DEFAULT_LOCK_TYPE,
	.shared_counter = 0,
	.total_wait_time = 0,
	.contention_count = 0,
	.threads = NULL,
	.last_run_result = SD_OK,
};

DEFINE_MUTEX(sync_control_mutex);

static int __init kernel_sync_init(void)
{
	if (!sync_num_threads_valid(sync_context.num_threads)) {
		pr_err("invalid num_threads=%u\n", sync_context.num_threads);
		return SD_INVALID;
	}

	if (!sync_iterations_valid(sync_context.iterations)) {
		pr_err("invalid iterations=%u\n", sync_context.iterations);
		return SD_INVALID;
	}

	if (!sync_lock_type_valid(sync_context.lock_type)) {
		pr_err("invalid lock_type=%u\n", sync_context.lock_type);
		return SD_INVALID;
	}

	sync_lock_init(&sync_context);
	atomic_set(&sync_context.threads_done, 0);

	pr_info("module loaded: threads=%u iterations=%u lock=%s\n",
		sync_context.num_threads, sync_context.iterations,
		sync_lock_name(sync_context.lock_type));

	return SD_OK;
}

static void __exit kernel_sync_exit(void)
{
	mutex_lock(&sync_control_mutex);
	sync_context.threads = NULL;
	mutex_unlock(&sync_control_mutex);

	pr_info("module unloaded\n");
}

module_init(kernel_sync_init);
module_exit(kernel_sync_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Chuprov");
MODULE_DESCRIPTION("HW-7: spinlock mutex semaphore kernel module");
