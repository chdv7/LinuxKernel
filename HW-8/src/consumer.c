#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "kernel_pc.h"

void tasklet_consumer(unsigned long data)
{
	struct pc_ctx *ctx = (struct pc_ctx *)data;
	unsigned int value;

	while (kfifo_get(&ctx->fifo, &value)) {
		ctx->sum += value;
		ctx->last_value = value;
		atomic_inc(&ctx->consumed);
	}
}

void work_consumer(struct work_struct *work)
{
	struct pc_ctx *ctx = container_of(work, struct pc_ctx, work);
	unsigned int value;
	int total;

	while (1) {
		if (kfifo_get(&ctx->fifo, &value)) {
			mutex_lock(&ctx->stats_lock);
			ctx->sum += value;
			ctx->last_value = value;
			mutex_unlock(&ctx->stats_lock);
			atomic_inc(&ctx->consumed);
			continue;
		}

		total = atomic_read(&ctx->produced) + atomic_read(&ctx->dropped);
		if (atomic_read(&ctx->stopping) || total >= ctx->num_events)
			break;

		msleep(1);
	}
}

int pc_consumer_start(struct pc_ctx *ctx)
{
	if (ctx->consumer_type == PC_CONSUMER_TASKLET) {
		tasklet_init(&ctx->tasklet, tasklet_consumer, (unsigned long)ctx);
		ctx->tasklet_initialized = true;
		return PC_OK;
	}

	INIT_WORK(&ctx->work, work_consumer);
	ctx->wq = create_singlethread_workqueue("pc_wq");
	if (!ctx->wq)
		return PC_NOMEM;

	return PC_OK;
}

void pc_consumer_schedule(struct pc_ctx *ctx)
{
	if (ctx->consumer_type == PC_CONSUMER_TASKLET) {
		if (ctx->tasklet_initialized)
			tasklet_schedule(&ctx->tasklet);
		return;
	}

	if (ctx->wq)
		queue_work(ctx->wq, &ctx->work);
}

void pc_consumer_stop(struct pc_ctx *ctx, bool drain)
{
	if (ctx->consumer_type == PC_CONSUMER_TASKLET) {
		if (!ctx->tasklet_initialized)
			return;

		if (drain)
			tasklet_schedule(&ctx->tasklet);

		tasklet_kill(&ctx->tasklet);
		ctx->tasklet_initialized = false;
		return;
	}

	if (!ctx->wq)
		return;

	if (drain)
		queue_work(ctx->wq, &ctx->work);

	flush_workqueue(ctx->wq);
	destroy_workqueue(ctx->wq);
	ctx->wq = NULL;
}
