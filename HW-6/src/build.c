#include "kernel_hashtable_search.h"

#include <linux/errno.h>
#include <linux/hashtable.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>

static void build_reset_result_locked(struct bucket_search_ctx *ctx)
{
	ctx->last_found = 0;
	ctx->last_value = 0;
	ctx->last_bucket = 0;
	ctx->current_bucket_id = 0;
}

static void build_clear_locked(struct bucket_search_ctx *ctx)
{
	struct hash_entry *entry;
	struct hlist_node *tmp;
	int bucket;

	hash_for_each_safe(ctx->htable, bucket, tmp, entry, node) {
		hash_del(&entry->node);
		kfree(entry);
	}

	kfree(ctx->array);
	ctx->array = NULL;
	hash_init(ctx->htable);
	build_reset_result_locked(ctx);
}

static int build_generate_locked(struct bucket_search_ctx *ctx)
{
	struct hash_entry *entry;
	unsigned int i;
	unsigned int value;

	ctx->array = kmalloc_array(ctx->array_size, sizeof(*ctx->array),
					 GFP_KERNEL);
	if (!ctx->array)
		return BS_NOMEM;

	for (i = 0; i < ctx->array_size; ++i) {
		value = get_random_u32_below(ctx->array_size);
		ctx->array[i] = value;

		entry = kmalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry) {
			build_clear_locked(ctx);
			return BS_NOMEM;
		}

		entry->value = value;
		hash_add(ctx->htable, &entry->node, value);
	}

	build_reset_result_locked(ctx);
	return BS_OK;
}

int build_init(struct bucket_search_ctx *ctx, unsigned int size)
{
	int ret;

	ctx->array_size = size;
	ctx->array = NULL;
	mutex_init(&ctx->lock);
	hash_init(ctx->htable);
	build_reset_result_locked(ctx);

	mutex_lock(&ctx->lock);
	ret = build_generate_locked(ctx);
	mutex_unlock(&ctx->lock);

	return ret;
}

int build_rebuild(struct bucket_search_ctx *ctx)
{
	int ret;

	mutex_lock(&ctx->lock);
	build_clear_locked(ctx);
	ret = build_generate_locked(ctx);
	mutex_unlock(&ctx->lock);

	return ret;
}

void build_destroy(struct bucket_search_ctx *ctx)
{
	mutex_lock(&ctx->lock);
	build_clear_locked(ctx);
	mutex_unlock(&ctx->lock);
}
