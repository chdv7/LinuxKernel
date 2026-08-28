#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/random.h>
#include <linux/wait.h>

#include "kernel_pc.h"

static unsigned long pc_run_timeout_jiffies(const struct pc_ctx *ctx)
{
	u64 timeout_ms;

	timeout_ms = DIV_ROUND_UP_ULL((u64)ctx->num_events * ctx->interval_us,
				       1000);
	timeout_ms += 5000;

	return msecs_to_jiffies((unsigned int)timeout_ms);
}

enum hrtimer_restart pc_timer_callback(struct hrtimer *timer)
{
	struct pc_ctx *ctx = container_of(timer, struct pc_ctx, timer);
	unsigned int value;
	int total;

	if (atomic_read(&ctx->stopping)) {
		wake_up(&ctx->producer_done_waitq);
		return HRTIMER_NORESTART;
	}

	total = atomic_read(&ctx->produced) + atomic_read(&ctx->dropped);
	if (total >= ctx->num_events) {
		wake_up(&ctx->producer_done_waitq);
		return HRTIMER_NORESTART;
	}

	value = get_random_u32() % 1000;
	if (!kfifo_put(&ctx->fifo, value))
		atomic_inc(&ctx->dropped);
	else
		atomic_inc(&ctx->produced);

	pc_consumer_schedule(ctx);

	total = atomic_read(&ctx->produced) + atomic_read(&ctx->dropped);
	if (total >= ctx->num_events) {
		wake_up(&ctx->producer_done_waitq);
		return HRTIMER_NORESTART;
	}

	hrtimer_forward_now(timer,
			    ns_to_ktime((u64)ctx->interval_us * NSEC_PER_USEC));
	return HRTIMER_RESTART;
}

int pc_run_test(struct pc_ctx *ctx)
{
	unsigned long timeout;
	long completed;
	int ret = PC_OK;

	if (!mutex_trylock(&pc_control_mutex))
		return PC_BUSY;

	if (!pc_fifo_size_valid(ctx->fifo_size) ||
	    !pc_num_events_valid(ctx->num_events) ||
	    !pc_interval_us_valid(ctx->interval_us) ||
	    !pc_consumer_type_valid(ctx->consumer_type)) {
		ret = PC_INVALID;
		goto out_unlock;
	}

	ctx->active = true;
	ctx->last_run_result = PC_OK;
	ctx->last_run_consumer_type = ctx->consumer_type;
	kfifo_reset(&ctx->fifo);
	pc_clear_run_state(ctx);

	ret = pc_consumer_start(ctx);
	if (ret)
		goto out_finish;

	timeout = pc_run_timeout_jiffies(ctx);
	hrtimer_start(&ctx->timer,
		      ns_to_ktime((u64)ctx->interval_us * NSEC_PER_USEC),
		      HRTIMER_MODE_REL);

	completed = wait_event_timeout(
		ctx->producer_done_waitq,
		atomic_read(&ctx->produced) + atomic_read(&ctx->dropped) >=
			ctx->num_events,
		timeout);

	if (!completed)
		ret = PC_TIMEOUT;

	atomic_set(&ctx->stopping, 1);
	hrtimer_cancel(&ctx->timer);
	pc_consumer_stop(ctx, true);

out_finish:
	ctx->active = false;
	ctx->last_run_result = ret;
out_unlock:
	mutex_unlock(&pc_control_mutex);
	return ret;
}
