#define pr_fmt(fmt) "msgpool: " fmt

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/timer.h>

#include "kernel_msgpool.h"

struct msgpool_ctx msgpool_context = {
	.alloc_type = MSGPOOL_DEFAULT_ALLOC_TYPE,
	.pool_min_nr = MSGPOOL_DEFAULT_POOL_MIN_NR,
	.interval_ms = MSGPOOL_DEFAULT_INTERVAL_MS,
	.msg_cache = NULL,
	.msg_pool = NULL,
	.seq_counter = 0,
	.initialized = false,
};

void msgpool_consumer_timer(struct timer_list *timer)
{
	struct msgpool_ctx *ctx =
		container_of(timer, struct msgpool_ctx, consumer_timer);
	struct msg *items[MSG_QUEUE_MAX];
	unsigned int alloc_type;
	unsigned int count;
	unsigned int i;
	unsigned long flags;

	count = msg_queue_drain(ctx, items, MSG_QUEUE_MAX, &alloc_type);

	for (i = 0; i < count; ++i) {
		s64 elapsed_ns;

		elapsed_ns = ktime_to_ns(
			ktime_sub(ktime_get(), items[i]->enqueue_time));

		pr_info("[%u] %s (queued %lld ns ago)\n", items[i]->seq,
			items[i]->text, (long long)elapsed_ns);

		spin_lock_irqsave(&ctx->last_msg_lock, flags);
		strscpy(ctx->last_msg, items[i]->text, sizeof(ctx->last_msg));
		spin_unlock_irqrestore(&ctx->last_msg_lock, flags);

		msg_free(ctx, items[i], alloc_type);
		atomic_inc(&ctx->consumed_total);
	}

	mod_timer(&ctx->consumer_timer,
		  jiffies + msecs_to_jiffies(READ_ONCE(ctx->interval_ms)));
}

static int __init kernel_msgpool_init(void)
{
	int ret;

	if (!msgpool_alloc_type_valid(msgpool_context.alloc_type)) {
		pr_err("invalid alloc_type=%u\n", msgpool_context.alloc_type);
		return MP_INVALID;
	}

	if (!msgpool_pool_min_nr_valid(msgpool_context.pool_min_nr)) {
		pr_err("invalid pool_min_nr=%u\n", msgpool_context.pool_min_nr);
		return MP_INVALID;
	}

	if (!msgpool_interval_valid(msgpool_context.interval_ms)) {
		pr_err("invalid interval_ms=%u\n", msgpool_context.interval_ms);
		return MP_INVALID;
	}

	msg_queue_init(&msgpool_context.queue);
	spin_lock_init(&msgpool_context.last_msg_lock);
	mutex_init(&msgpool_context.control_lock);
	msgpool_context.last_msg[0] = '\0';

	atomic_set(&msgpool_context.sent_total, 0);
	atomic_set(&msgpool_context.consumed_total, 0);
	atomic_set(&msgpool_context.flushed_total, 0);
	atomic_set(&msgpool_context.dropped_total, 0);

	ret = msg_allocator_init(&msgpool_context);
	if (ret)
		return ret;

	timer_setup(&msgpool_context.consumer_timer, msgpool_consumer_timer, 0);
	WRITE_ONCE(msgpool_context.initialized, true);
	mod_timer(&msgpool_context.consumer_timer,
		  jiffies + msecs_to_jiffies(msgpool_context.interval_ms));

	pr_info("module loaded: alloc=%s pool_min_nr=%u interval_ms=%u\n",
		msgpool_alloc_name(msgpool_context.alloc_type),
		msgpool_context.pool_min_nr, msgpool_context.interval_ms);

	return MP_OK;
}

static void __exit kernel_msgpool_exit(void)
{
	unsigned int pending;

	WRITE_ONCE(msgpool_context.initialized, false);
	timer_shutdown_sync(&msgpool_context.consumer_timer);

	mutex_lock(&msgpool_context.control_lock);
	pending = msgpool_flush_pending(&msgpool_context, false);
	mutex_unlock(&msgpool_context.control_lock);

	msg_allocator_destroy(&msgpool_context);

	pr_info("module unloaded: pending_freed=%u sent=%d consumed=%d flushed=%d dropped=%d\n",
		pending, atomic_read(&msgpool_context.sent_total),
		atomic_read(&msgpool_context.consumed_total),
		atomic_read(&msgpool_context.flushed_total),
		atomic_read(&msgpool_context.dropped_total));
}

module_init(kernel_msgpool_init);
module_exit(kernel_msgpool_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Chuprov");
MODULE_DESCRIPTION("HW-10: kmem_cache and mempool message queue");
