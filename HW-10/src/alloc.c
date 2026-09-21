#define pr_fmt(fmt) "msgpool: " fmt

#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/mempool.h>
#include <linux/printk.h>
#include <linux/slab.h>

#include "kernel_msgpool.h"

bool msgpool_alloc_type_valid(unsigned int value)
{
	return value == MSGPOOL_ALLOC_CACHE || value == MSGPOOL_ALLOC_MEMPOOL;
}

bool msgpool_pool_min_nr_valid(unsigned int value)
{
	return value >= MSGPOOL_MIN_POOL_MIN_NR &&
	       value <= MSGPOOL_MAX_POOL_MIN_NR;
}

bool msgpool_interval_valid(unsigned int value)
{
	return value >= MSGPOOL_MIN_INTERVAL_MS &&
	       value <= MSGPOOL_MAX_INTERVAL_MS;
}

const char *msgpool_alloc_name(unsigned int alloc_type)
{
	return alloc_type == MSGPOOL_ALLOC_MEMPOOL ? "mempool" : "kmem_cache";
}

int msg_allocator_init(struct msgpool_ctx *ctx)
{
	ctx->msg_cache = kmem_cache_create("msgpool_cache", sizeof(struct msg),
					   0, SLAB_HWCACHE_ALIGN, NULL);
	if (!ctx->msg_cache)
		return MP_NOMEM;

	ctx->msg_pool = mempool_create_slab_pool(ctx->pool_min_nr,
						 ctx->msg_cache);
	if (!ctx->msg_pool) {
		kmem_cache_destroy(ctx->msg_cache);
		ctx->msg_cache = NULL;
		return MP_NOMEM;
	}

	return MP_OK;
}

void msg_allocator_destroy(struct msgpool_ctx *ctx)
{
	if (ctx->msg_pool) {
		mempool_destroy(ctx->msg_pool);
		ctx->msg_pool = NULL;
	}

	if (ctx->msg_cache) {
		kmem_cache_destroy(ctx->msg_cache);
		ctx->msg_cache = NULL;
	}
}

struct msg *msg_alloc(struct msgpool_ctx *ctx, unsigned int alloc_type,
		      gfp_t gfp)
{
	if (alloc_type == MSGPOOL_ALLOC_MEMPOOL)
		return mempool_alloc(ctx->msg_pool, gfp);

	return kmem_cache_alloc(ctx->msg_cache, gfp);
}

void msg_free(struct msgpool_ctx *ctx, struct msg *obj,
	      unsigned int alloc_type)
{
	if (!obj)
		return;

	if (alloc_type == MSGPOOL_ALLOC_MEMPOOL)
		mempool_free(obj, ctx->msg_pool);
	else
		kmem_cache_free(ctx->msg_cache, obj);
}
