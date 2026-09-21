#define pr_fmt(fmt) "msgpool: " fmt

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/spinlock.h>

#include "kernel_msgpool.h"

void msg_queue_init(struct msg_queue *queue)
{
	unsigned int i;

	queue->head = 0;
	queue->tail = 0;
	queue->count = 0;
	spin_lock_init(&queue->lock);

	for (i = 0; i < MSG_QUEUE_MAX; ++i)
		queue->slots[i] = NULL;
}

int msg_queue_enqueue(struct msg_queue *queue, struct msg *obj)
{
	unsigned long flags;
	int ret = MP_OK;

	spin_lock_irqsave(&queue->lock, flags);
	if (queue->count >= MSG_QUEUE_MAX) {
		ret = MP_FULL;
	} else {
		queue->slots[queue->tail] = obj;
		queue->tail = (queue->tail + 1) % MSG_QUEUE_MAX;
		queue->count++;
	}
	spin_unlock_irqrestore(&queue->lock, flags);

	return ret;
}

unsigned int msg_queue_drain(struct msgpool_ctx *ctx, struct msg **items,
			     unsigned int max_items,
			     unsigned int *alloc_type)
{
	struct msg_queue *queue = &ctx->queue;
	unsigned long flags;
	unsigned int count = 0;

	spin_lock_irqsave(&queue->lock, flags);
	*alloc_type = ctx->alloc_type;

	while (queue->count > 0 && count < max_items) {
		items[count] = queue->slots[queue->head];
		queue->slots[queue->head] = NULL;
		queue->head = (queue->head + 1) % MSG_QUEUE_MAX;
		queue->count--;
		count++;
	}
	spin_unlock_irqrestore(&queue->lock, flags);

	return count;
}

unsigned int msg_queue_count(struct msg_queue *queue)
{
	unsigned long flags;
	unsigned int count;

	spin_lock_irqsave(&queue->lock, flags);
	count = queue->count;
	spin_unlock_irqrestore(&queue->lock, flags);

	return count;
}

bool msg_queue_empty(struct msg_queue *queue)
{
	return msg_queue_count(queue) == 0;
}

unsigned int msgpool_flush_pending(struct msgpool_ctx *ctx, bool count_flush)
{
	struct msg *items[MSG_QUEUE_MAX];
	unsigned int alloc_type;
	unsigned int count;
	unsigned int i;

	count = msg_queue_drain(ctx, items, MSG_QUEUE_MAX, &alloc_type);

	for (i = 0; i < count; ++i) {
		msg_free(ctx, items[i], alloc_type);
		if (count_flush)
			atomic_inc(&ctx->flushed_total);
	}

	return count;
}
