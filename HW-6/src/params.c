#include "kernel_hashtable_search.h"

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>

unsigned int array_size = DEFAULT_ARRAY_SIZE;

static int array_size_set(const char *value, const struct kernel_param *kp)
{
	unsigned int parsed;
	int ret;

	ret = kstrtouint(value, 0, &parsed);
	if (ret)
		return BS_INVALID;

	if (parsed < MIN_ARRAY_SIZE || parsed > MAX_ARRAY_SIZE)
		return BS_INVALID;

	*(unsigned int *)kp->arg = parsed;
	return BS_OK;
}

static int array_size_get(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%u\n", *(unsigned int *)kp->arg);
}

static const struct kernel_param_ops array_size_ops = {
	.set = array_size_set,
	.get = array_size_get,
};

static int search_set(const char *value, const struct kernel_param *kp)
{
	unsigned int parsed;
	int ret;

	ret = kstrtouint(value, 0, &parsed);
	if (ret)
		return BS_INVALID;

	ret = search_value(&search_ctx, parsed);
	if (ret == BS_NOMEM)
		return ret;

	return BS_OK;
}

static const struct kernel_param_ops search_ops = {
	.set = search_set,
};

static int result_get(char *buffer, const struct kernel_param *kp)
{
	return search_format_result(&search_ctx, buffer, PAGE_SIZE);
}

static const struct kernel_param_ops result_ops = {
	.get = result_get,
};

static int rebuild_set(const char *value, const struct kernel_param *kp)
{
	unsigned int parsed;
	int ret;

	ret = kstrtouint(value, 0, &parsed);
	if (ret)
		return BS_INVALID;

	if (parsed == 0)
		return BS_OK;

	return build_rebuild(&search_ctx);
}

static const struct kernel_param_ops rebuild_ops = {
	.set = rebuild_set,
};

static int bucket_id_set(const char *value, const struct kernel_param *kp)
{
	unsigned int parsed;
	int ret;

	ret = kstrtouint(value, 0, &parsed);
	if (ret)
		return BS_INVALID;

	return search_set_bucket(&search_ctx, parsed);
}

static const struct kernel_param_ops bucket_id_ops = {
	.set = bucket_id_set,
};

static int bucket_dump_get(char *buffer, const struct kernel_param *kp)
{
	return search_format_bucket_dump(&search_ctx, buffer, PAGE_SIZE);
}

static const struct kernel_param_ops bucket_dump_ops = {
	.get = bucket_dump_get,
};

module_param_cb(array_size, &array_size_ops, &array_size, 0444);
MODULE_PARM_DESC(array_size,
		 "Array size and generated value upper bound, range 1..1000000");

module_param_cb(search, &search_ops, NULL, 0200);
MODULE_PARM_DESC(search, "Search value in hashtable bucket using bsearch");

module_param_cb(result, &result_ops, NULL, 0444);
MODULE_PARM_DESC(result, "Last search result");

module_param_cb(rebuild, &rebuild_ops, NULL, 0200);
MODULE_PARM_DESC(rebuild, "Rebuild hashtable with newly generated random values");

module_param_cb(bucket_id, &bucket_id_ops, NULL, 0200);
MODULE_PARM_DESC(bucket_id, "Bucket id for bucket_dump, range 0..63");

module_param_cb(bucket_dump, &bucket_dump_ops, NULL, 0444);
MODULE_PARM_DESC(bucket_dump, "Sorted values from selected bucket");
