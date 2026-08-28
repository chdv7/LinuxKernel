#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include "kernel_sync.h"

static DECLARE_WAIT_QUEUE_HEAD(done_waitq);

static void worker_update_counter(struct worker_args *args, long long delta)
{
	struct sync_ctx *ctx = args->ctx;
	ktime_t t_before;
	ktime_t t_after;
	s64 wait_ns;

	t_before = ktime_get();
	sync_lock_acquire(ctx);
	t_after = ktime_get();

	ctx->shared_counter += delta;

	sync_lock_release(ctx);

	wait_ns = ktime_to_ns(ktime_sub(t_after, t_before));
	if (wait_ns > SYNC_WAIT_THRESHOLD_NS) {
		args->wait_time = ktime_add_ns(args->wait_time, wait_ns);
		args->contention_count++;
	}
}

static int worker_thread(void *data)
{
	struct worker_args *args = data;
	unsigned int i;

	for (i = 0; i < args->ctx->iterations; ++i) {
		if (kthread_should_stop())
			break;

		worker_update_counter(args, 1);
		worker_update_counter(args, -1);
	}

	atomic_inc(&args->ctx->threads_done);
	wake_up(&done_waitq);

	return 0;
}

static void sync_clear_run_state(struct sync_ctx *ctx)
{
	ctx->shared_counter = 0;
	ctx->total_wait_time = 0;
	ctx->contention_count = 0;
	ctx->last_run_result = SD_OK;
	atomic_set(&ctx->threads_done, 0);
}

static void sync_collect_stats(struct sync_ctx *ctx, struct worker_args *args)
{
	unsigned int i;

	ctx->total_wait_time = 0;
	ctx->contention_count = 0;

	for (i = 0; i < ctx->num_threads; ++i) {
		ctx->total_wait_time =
			ktime_add(ctx->total_wait_time, args[i].wait_time);
		ctx->contention_count += args[i].contention_count;
	}
}

int sync_run_test(struct sync_ctx *ctx)
{
	struct worker_args *args;
	unsigned int created = 0;
	unsigned int i;
	int ret = SD_OK;

	if (!mutex_trylock(&sync_control_mutex))
		return SD_BUSY;

	if (!sync_num_threads_valid(ctx->num_threads) ||
	    !sync_iterations_valid(ctx->iterations) ||
	    !sync_lock_type_valid(ctx->lock_type)) {
		ret = SD_INVALID;
		goto out_unlock;
	}

	sync_lock_init(ctx);
	sync_clear_run_state(ctx);

	ctx->threads = kcalloc(ctx->num_threads, sizeof(*ctx->threads),
				     GFP_KERNEL);
	if (!ctx->threads) {
		ret = SD_NOMEM;
		goto out_finish;
	}

	args = kcalloc(ctx->num_threads, sizeof(*args), GFP_KERNEL);
	if (!args) {
		ret = SD_NOMEM;
		goto out_free_threads;
	}

	for (i = 0; i < ctx->num_threads; ++i) {
		args[i].ctx = ctx;
		args[i].thread_id = i;
		args[i].wait_time = 0;
		args[i].contention_count = 0;

		ctx->threads[i] = kthread_create(worker_thread, &args[i],
						  "kernel_sync/%u", i);
		if (IS_ERR(ctx->threads[i])) {
			ret = PTR_ERR(ctx->threads[i]);
			ctx->threads[i] = NULL;
			break;
		}

		created++;
		wake_up_process(ctx->threads[i]);
	}

	wait_event(done_waitq, atomic_read(&ctx->threads_done) >= created);
	sync_collect_stats(ctx, args);

	if (ret == SD_OK && ctx->shared_counter != 0)
		ret = -EIO;

	kfree(args);

out_free_threads:
	kfree(ctx->threads);
	ctx->threads = NULL;

out_finish:
	ctx->last_run_result = ret;
out_unlock:
	mutex_unlock(&sync_control_mutex);
	return ret;
}
