#ifndef KERNEL_HASHTABLE_SEARCH_H_
#define KERNEL_HASHTABLE_SEARCH_H_

#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define HASH_BITS_COUNT 6
#define HASH_BUCKETS_COUNT (1U << HASH_BITS_COUNT)

#define DEFAULT_ARRAY_SIZE 1024U
#define MIN_ARRAY_SIZE 1U
#define MAX_ARRAY_SIZE 1000000U

#define BS_OK 0
#define BS_INVALID -EINVAL
#define BS_NOMEM -ENOMEM
#define BS_NOT_FOUND -ENOENT

struct hash_entry {
	struct hlist_node node;
	unsigned int value;
};

struct bucket_search_ctx {
	unsigned int array_size;
	unsigned int *array;

	DECLARE_HASHTABLE(htable, HASH_BITS_COUNT);

	int last_found;
	unsigned int last_value;
	unsigned int last_bucket;

	unsigned int current_bucket_id;

	struct mutex lock;
};

extern struct bucket_search_ctx search_ctx;
extern unsigned int array_size;

int build_init(struct bucket_search_ctx *ctx, unsigned int size);
int build_rebuild(struct bucket_search_ctx *ctx);
void build_destroy(struct bucket_search_ctx *ctx);

int search_value(struct bucket_search_ctx *ctx, unsigned int value);
int search_set_bucket(struct bucket_search_ctx *ctx, unsigned int bucket_id);
int search_format_result(struct bucket_search_ctx *ctx, char *buffer, size_t size);
int search_format_bucket_dump(struct bucket_search_ctx *ctx, char *buffer,
				      size_t size);

#endif  // KERNEL_HASHTABLE_SEARCH_H_
