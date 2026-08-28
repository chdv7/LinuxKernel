#include "kernel_hashtable_search.h"

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

struct bucket_search_ctx search_ctx;

static int __init kernel_hashtable_search_init(void)
{
	int ret;

	if (array_size < MIN_ARRAY_SIZE || array_size > MAX_ARRAY_SIZE)
		return BS_INVALID;

	ret = build_init(&search_ctx, array_size);
	if (ret)
		return ret;

	pr_info("kernel_hashtable_search: loaded, array_size=%u buckets=%u\n",
		array_size, HASH_BUCKETS_COUNT);
	return BS_OK;
}

static void __exit kernel_hashtable_search_exit(void)
{
	build_destroy(&search_ctx);
	pr_info("kernel_hashtable_search: unloaded\n");
}

module_init(kernel_hashtable_search_init);
module_exit(kernel_hashtable_search_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Chuprov");
MODULE_DESCRIPTION("HW hashtable and binary search kernel module");
