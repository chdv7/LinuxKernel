#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/version.h>

#include "kernel_pc.h"

struct pc_ctx pc_context = {
	.fifo_size = PC_DEFAULT_FIFO_SIZE,
	.num_events = PC_DEFAULT_NUM_EVENTS,
	.interval_us = PC_DEFAULT_INTERVAL_US,
	.consumer_type = PC_DEFAULT_CONSUMER_TYPE,
	.wq = NULL,
	.active = false,
	.tasklet_initialized = false,
	.last_run_consumer_type = PC_DEFAULT_CONSUMER_TYPE,
	.last_run_result = PC_OK,
};

DEFINE_MUTEX(pc_control_mutex);
bool pc_module_ready;

bool pc_fifo_size_valid(unsigned int value)
{
	return value >= PC_MIN_FIFO_SIZE && value <= PC_MAX_FIFO_SIZE &&
	       (value & (value - 1)) == 0;
}

bool pc_num_events_valid(unsigned int value)
{
	return value >= PC_MIN_NUM_EVENTS && value <= PC_MAX_NUM_EVENTS;
}

bool pc_interval_us_valid(unsigned int value)
{
	return value >= PC_MIN_INTERVAL_US && value <= PC_MAX_INTERVAL_US;
}

bool pc_consumer_type_valid(unsigned int value)
{
	return value == PC_CONSUMER_TASKLET || value == PC_CONSUMER_WORKQUEUE;
}

const char *pc_consumer_name(unsigned int consumer_type)
{
	return consumer_type == PC_CONSUMER_WORKQUEUE ? "workqueue" : "tasklet";
}

void pc_clear_run_state(struct pc_ctx *ctx)
{
	atomic_set(&ctx->produced, 0);
	atomic_set(&ctx->dropped, 0);
	atomic_set(&ctx->consumed, 0);
	atomic_set(&ctx->stopping, 0);
	ctx->sum = 0;
	ctx->last_value = 0;
}

static int __init kernel_pc_init(void)
{
	int ret;

	if (!pc_fifo_size_valid(pc_context.fifo_size)) {
		pr_err("invalid fifo_size=%u\n", pc_context.fifo_size);
		return PC_INVALID;
	}

	if (!pc_num_events_valid(pc_context.num_events)) {
		pr_err("invalid num_events=%u\n", pc_context.num_events);
		return PC_INVALID;
	}

	if (!pc_interval_us_valid(pc_context.interval_us)) {
		pr_err("invalid interval_us=%u\n", pc_context.interval_us);
		return PC_INVALID;
	}

	if (!pc_consumer_type_valid(pc_context.consumer_type)) {
		pr_err("invalid consumer_type=%u\n", pc_context.consumer_type);
		return PC_INVALID;
	}

	mutex_init(&pc_context.stats_lock);
	init_waitqueue_head(&pc_context.producer_done_waitq);
	pc_clear_run_state(&pc_context);

	ret = kfifo_alloc(&pc_context.fifo, pc_context.fifo_size, GFP_KERNEL);
	if (ret) {
		pr_err("kfifo_alloc(%u) failed: %d\n", pc_context.fifo_size, ret);
		return PC_NOMEM;
	}

	hrtimer_setup(&pc_context.timer, pc_timer_callback, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL);
	pc_module_ready = true;

	pr_info("module loaded: fifo=%u events=%u interval_us=%u consumer=%s\n",
		pc_context.fifo_size, pc_context.num_events,
		pc_context.interval_us,
		pc_consumer_name(pc_context.consumer_type));

	return PC_OK;
}

static void __exit kernel_pc_exit(void)
{
	pc_module_ready = false;

	mutex_lock(&pc_control_mutex);
	atomic_set(&pc_context.stopping, 1);
	hrtimer_cancel(&pc_context.timer);
	pc_consumer_stop(&pc_context, false);
	pc_context.active = false;
	mutex_unlock(&pc_control_mutex);

	kfifo_free(&pc_context.fifo);
	pr_info("module unloaded\n");
}

module_init(kernel_pc_init);
module_exit(kernel_pc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Chuprov");
MODULE_DESCRIPTION("HW-8: producer consumer tasklet workqueue kernel module");
