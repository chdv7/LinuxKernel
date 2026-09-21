#define pr_fmt(fmt) "msgpool: " fmt

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/string.h>

#include "kernel_msgpool.h"

static int parse_uint_param(const char *val, unsigned int *value)
{
	return kstrtouint(val, 0, value);
}

static int set_alloc_type(const char *val, const struct kernel_param *kp)
{
	struct msgpool_ctx *ctx = &msgpool_context;
	unsigned long flags;
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (!msgpool_alloc_type_valid(value))
		return MP_INVALID;

	if (!READ_ONCE(ctx->initialized)) {
		ctx->alloc_type = value;
		return MP_OK;
	}

	mutex_lock(&ctx->control_lock);
	spin_lock_irqsave(&ctx->queue.lock, flags);
	if (ctx->queue.count != 0) {
		ret = MP_BUSY;
	} else {
		ctx->alloc_type = value;
		ret = MP_OK;
	}
	spin_unlock_irqrestore(&ctx->queue.lock, flags);
	mutex_unlock(&ctx->control_lock);

	return ret;
}

static int get_alloc_type(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n",
			 READ_ONCE(msgpool_context.alloc_type));
}

static const struct kernel_param_ops alloc_type_ops = {
	.set = set_alloc_type,
	.get = get_alloc_type,
};

module_param_cb(alloc_type, &alloc_type_ops, NULL, 0644);
MODULE_PARM_DESC(alloc_type, "Allocator: 0 kmem_cache, 1 mempool");

static int set_pool_min_nr(const char *val, const struct kernel_param *kp)
{
	struct msgpool_ctx *ctx = &msgpool_context;
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (!msgpool_pool_min_nr_valid(value))
		return MP_INVALID;

	if (!READ_ONCE(ctx->initialized)) {
		ctx->pool_min_nr = value;
		return MP_OK;
	}

	mutex_lock(&ctx->control_lock);
	if (!msg_queue_empty(&ctx->queue)) {
		ret = MP_BUSY;
		goto out_unlock;
	}

	ret = mempool_resize(ctx->msg_pool, value);
	if (!ret)
		ctx->pool_min_nr = value;

out_unlock:
	mutex_unlock(&ctx->control_lock);
	return ret;
}

static int get_pool_min_nr(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n",
			 READ_ONCE(msgpool_context.pool_min_nr));
}

static const struct kernel_param_ops pool_min_nr_ops = {
	.set = set_pool_min_nr,
	.get = get_pool_min_nr,
};

module_param_cb(pool_min_nr, &pool_min_nr_ops, NULL, 0644);
MODULE_PARM_DESC(pool_min_nr, "Minimum mempool reserve, 1..64");

static int set_interval_ms(const char *val, const struct kernel_param *kp)
{
	struct msgpool_ctx *ctx = &msgpool_context;
	unsigned int value;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (!msgpool_interval_valid(value))
		return MP_INVALID;

	WRITE_ONCE(ctx->interval_ms, value);
	if (READ_ONCE(ctx->initialized))
		mod_timer(&ctx->consumer_timer,
			  jiffies + msecs_to_jiffies(value));

	return MP_OK;
}

static int get_interval_ms(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n",
			 READ_ONCE(msgpool_context.interval_ms));
}

static const struct kernel_param_ops interval_ms_ops = {
	.set = set_interval_ms,
	.get = get_interval_ms,
};

module_param_cb(interval_ms, &interval_ms_ops, NULL, 0644);
MODULE_PARM_DESC(interval_ms, "Consumer timer interval in ms, 100..60000");

static int set_send(const char *val, const struct kernel_param *kp)
{
	struct msgpool_ctx *ctx = &msgpool_context;
	struct msg *obj;
	unsigned int alloc_type;
	size_t newline;
	ssize_t copied;
	int ret;

	if (!READ_ONCE(ctx->initialized))
		return MP_BUSY;

	mutex_lock(&ctx->control_lock);
	alloc_type = ctx->alloc_type;

	obj = msg_alloc(ctx, alloc_type, GFP_KERNEL);
	if (!obj) {
		ret = MP_NOMEM;
		goto out_unlock;
	}

	copied = strscpy(obj->text, val, sizeof(obj->text));
	if (copied < 0)
		pr_warn("message truncated to %u bytes\n", MSG_TEXT_MAX - 1);

	newline = strcspn(obj->text, "\r\n");
	obj->text[newline] = '\0';
	obj->enqueue_time = ktime_get();
	obj->seq = ++ctx->seq_counter;

	ret = msg_queue_enqueue(&ctx->queue, obj);
	if (ret) {
		msg_free(ctx, obj, alloc_type);
		atomic_inc(&ctx->dropped_total);
		goto out_unlock;
	}

	atomic_inc(&ctx->sent_total);

out_unlock:
	mutex_unlock(&ctx->control_lock);
	return ret;
}

static const struct kernel_param_ops send_ops = {
	.set = set_send,
};

module_param_cb(send, &send_ops, NULL, 0200);
MODULE_PARM_DESC(send, "Write a message to enqueue it");

static int get_inbox(char *buffer, const struct kernel_param *kp)
{
	struct msgpool_ctx *ctx = &msgpool_context;
	char last[MSG_TEXT_MAX];
	unsigned long flags;

	spin_lock_irqsave(&ctx->last_msg_lock, flags);
	strscpy(last, ctx->last_msg, sizeof(last));
	spin_unlock_irqrestore(&ctx->last_msg_lock, flags);

	if (last[0] == '\0')
		return scnprintf(buffer, PAGE_SIZE, "(empty)\n");

	return scnprintf(buffer, PAGE_SIZE, "%s\n", last);
}

static const struct kernel_param_ops inbox_ops = {
	.get = get_inbox,
};

module_param_cb(inbox, &inbox_ops, NULL, 0444);
MODULE_PARM_DESC(inbox, "Last message processed by the consumer timer");

static int get_stats(char *buffer, const struct kernel_param *kp)
{
	struct msgpool_ctx *ctx = &msgpool_context;
	unsigned int queued;
	unsigned int alloc_type;

	queued = msg_queue_count(&ctx->queue);
	alloc_type = READ_ONCE(ctx->alloc_type);

	return scnprintf(buffer, PAGE_SIZE,
			 "sent=%d consumed=%d flushed=%d dropped=%d queued=%u alloc=%s interval_ms=%u\n",
			 atomic_read(&ctx->sent_total),
			 atomic_read(&ctx->consumed_total),
			 atomic_read(&ctx->flushed_total),
			 atomic_read(&ctx->dropped_total), queued,
			 msgpool_alloc_name(alloc_type),
			 READ_ONCE(ctx->interval_ms));
}

static const struct kernel_param_ops stats_ops = {
	.get = get_stats,
};

module_param_cb(stats, &stats_ops, NULL, 0444);
MODULE_PARM_DESC(stats, "Message queue statistics");

static int set_flush(const char *val, const struct kernel_param *kp)
{
	struct msgpool_ctx *ctx = &msgpool_context;
	unsigned int value;
	unsigned int count;
	int ret;

	ret = parse_uint_param(val, &value);
	if (ret)
		return ret;

	if (value == 0)
		return MP_INVALID;

	if (!READ_ONCE(ctx->initialized))
		return MP_BUSY;

	mutex_lock(&ctx->control_lock);
	count = msgpool_flush_pending(ctx, true);
	mutex_unlock(&ctx->control_lock);

	pr_info("flushed %u message(s)\n", count);
	return MP_OK;
}

static const struct kernel_param_ops flush_ops = {
	.set = set_flush,
};

module_param_cb(flush, &flush_ops, NULL, 0200);
MODULE_PARM_DESC(flush, "Write non-zero value to flush queued messages");
