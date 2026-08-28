#include "kernel_hashtable_search.h"

#include <linux/bsearch.h>
#include <linux/errno.h>
#include <linux/hashtable.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sort.h>

// аналог  <=> в C++
static int hash_entry_ptr_cmp(const void *left, const void *right)
{
	const struct hash_entry *left_entry =
		*(const struct hash_entry * const *)left;
	const struct hash_entry *right_entry =
		*(const struct hash_entry * const *)right;

	if (left_entry->value < right_entry->value)
		return -1;
	if (left_entry->value > right_entry->value)
		return 1;
	return 0;
}

static int hash_entry_bsearch_cmp(const void *key, const void *element)
{
	unsigned int value = *(const unsigned int *)key;
	const struct hash_entry *entry =
		*(const struct hash_entry * const *)element;

	if (value < entry->value)
		return -1;
	if (value > entry->value)
		return 1;
	return 0;
}

// собирает все элементы выбранной корзины bucket_id во временный массив указателей struct hash_entry **. 
//  Сначала считает длину bucket через hlist_for_each_entry(), затем выделяет массив и заполняет его указателями 
//  на элементы. Вызывается под ctx->lock.
static int search_collect_bucket_locked(struct bucket_search_ctx *ctx,
					unsigned int bucket_id,
					struct hash_entry ***entries,
					size_t *count)
{
	struct hash_entry **array;
	struct hash_entry *entry;
	size_t len = 0;
	size_t i = 0;

	hlist_for_each_entry(entry, &ctx->htable[bucket_id], node)
		++len;

	*entries = NULL;
	*count = len;

	if (len == 0)
		return BS_OK;

	array = kmalloc_array(len, sizeof(*array), GFP_KERNEL);
	if (!array)
		return BS_NOMEM;

	hlist_for_each_entry(entry, &ctx->htable[bucket_id], node)
		array[i++] = entry;

	*entries = array;
	return BS_OK;
}

int search_value(struct bucket_search_ctx *ctx, unsigned int value)
{
	struct hash_entry **entries;
	struct hash_entry **found;
	unsigned int bucket_id;
	size_t count;
	int ret;

	// получили bucket_id
	bucket_id = hash_min(value, HASH_BITS_COUNT);

	mutex_lock(&ctx->lock);

	ctx->last_value = value;
	ctx->last_bucket = bucket_id;
	ctx->last_found = 0;

	if (value >= ctx->array_size) {
		mutex_unlock(&ctx->lock);
		return BS_NOT_FOUND;
	}

	ret = search_collect_bucket_locked(ctx, bucket_id, &entries, &count);
	if (ret != BS_OK) {
		mutex_unlock(&ctx->lock);
		return ret;
	}
	// entries - временный массив длинной count - Надо освобождать
	sort(entries, count, sizeof(*entries), hash_entry_ptr_cmp, NULL);

	found = bsearch(&value, entries, count, sizeof(*entries),
			 hash_entry_bsearch_cmp);
	if (found)
		ctx->last_found = 1;

	kfree(entries);
	mutex_unlock(&ctx->lock);

	return found ? BS_OK : BS_NOT_FOUND;
}

// устанавливает текущий bucket для последующего чтения через bucket_dump. 
// Проверяет, что bucket_id < HASH_BUCKETS_COUNT
int search_set_bucket(struct bucket_search_ctx *ctx, unsigned int bucket_id)
{
	if (bucket_id >= HASH_BUCKETS_COUNT)
		return BS_INVALID;

	mutex_lock(&ctx->lock);
	ctx->current_bucket_id = bucket_id;
	mutex_unlock(&ctx->lock);

	return BS_OK;
}

int search_format_result(struct bucket_search_ctx *ctx, char *buffer, size_t size)
{
	int len;

	mutex_lock(&ctx->lock);
	len = scnprintf(buffer, size, "found=%d value=%u bucket=%u\n",
			ctx->last_found, ctx->last_value, ctx->last_bucket);
	mutex_unlock(&ctx->lock);

	return len;
}
// формирует содержимое выбранной корзины для чтения параметра bucket_dump. 
// Собирает элементы bucket, 
// сортирует их по value, 
// затем выводит строку вида bucket=5 len=7: 1 5 9 13 17 21 25.
int search_format_bucket_dump(struct bucket_search_ctx *ctx, char *buffer,
				      size_t size)
{
	struct hash_entry **entries;
	unsigned int bucket_id;
	size_t count;
	size_t i;
	int ret;
	int len = 0;

	mutex_lock(&ctx->lock);

	bucket_id = ctx->current_bucket_id;
	if (bucket_id >= HASH_BUCKETS_COUNT) {
		mutex_unlock(&ctx->lock);
		return scnprintf(buffer, size, "error=invalid_bucket bucket=%u\n",
				 bucket_id);
	}

	ret = search_collect_bucket_locked(ctx, bucket_id, &entries, &count);
	if (ret != BS_OK) {
		mutex_unlock(&ctx->lock);
		return ret;
	}

	sort(entries, count, sizeof(*entries), hash_entry_ptr_cmp, NULL);

	len += scnprintf(buffer + len, size - len, "bucket=%u len=%zu:",
			 bucket_id, count);

	for (i = 0; i < count && len < size - 16; ++i)
		len += scnprintf(buffer + len, size - len, " %u",
				 entries[i]->value);

	if (i < count && len < size - 8)
		len += scnprintf(buffer + len, size - len, " ...");

	if (len < size - 1)
		len += scnprintf(buffer + len, size - len, "\n");

	kfree(entries);
	mutex_unlock(&ctx->lock);

	return len;
}
