#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>

#include "kernel_alloc.h"

static void *last_alloc;

static int allocator_status_to_errno(int status)
{
	switch (status) {
	case ALLOC_OK:
		return 0;
	case ALLOC_NOMEM:
		return -ENOMEM;
	case ALLOC_INVALID:
		return -EINVAL;
	case ALLOC_NOT_FOUND:
		return -ENOENT;
	default:
		return -EINVAL;
	}
}

static int parse_address(const char *val, unsigned long *addr)
{
	int ret;

	ret = kstrtoul(val, 0, addr);
	if (!ret)
		return 0;

	return kstrtoul(val, 16, addr);
}

static int param_set_alloc(const char *val, const struct kernel_param *kp)
{
	unsigned long bytes;
	void *ptr;
	int ret;

	ret = kstrtoul(val, 0, &bytes);
	if (ret)
		return ret;

	if (bytes == 0) {
		pr_err("allocation of 0 bytes is invalid\n");
		return -EINVAL;
	}

	ptr = allocator_alloc((size_t)bytes);
	if (!ptr) {
		pr_err("failed to allocate %lu bytes\n", bytes);
		return -ENOMEM;
	}

	last_alloc = ptr;
	pr_info("allocated %lu bytes at 0x%px\n", bytes, ptr);

	return 0;
}

static int param_get_alloc(char *buffer, const struct kernel_param *kp)
{
	if (!last_alloc)
		return scnprintf(buffer, PAGE_SIZE, "Last allocation: none\n");

	return scnprintf(buffer, PAGE_SIZE, "Last allocation: 0x%px\n",
			 last_alloc);
}

static const struct kernel_param_ops alloc_param_ops = {
	.set = param_set_alloc,
	.get = param_get_alloc,
};

module_param_cb(alloc, &alloc_param_ops, NULL, 0644);
MODULE_PARM_DESC(alloc, "Allocate memory, value is size in bytes");

static int param_set_free(const char *val, const struct kernel_param *kp)
{
	unsigned long addr;
	void *ptr;
	int ret;
	int status;

	ret = parse_address(val, &addr);
	if (ret)
		return ret;

	ptr = (void *)addr;
	status = allocator_free(ptr);
	if (status != ALLOC_OK) {
		pr_err("failed to free memory at 0x%lx: status=%d\n", addr,
		       status);
		return allocator_status_to_errno(status);
	}

	if (last_alloc == ptr)
		last_alloc = NULL;

	pr_info("freed memory at 0x%lx\n", addr);

	return 0;
}

static const struct kernel_param_ops free_param_ops = {
	.set = param_set_free,
};

module_param_cb(free, &free_param_ops, NULL, 0644);
MODULE_PARM_DESC(free, "Free memory, value is allocated address");


static int param_get_stats(char *buffer, const struct kernel_param *kp)
{
	struct stats_info stats = allocator_get_stats();

	return scnprintf(buffer, PAGE_SIZE,
			 "Total: %zu KB | Free: %zu KB | Allocated: %zu KB | Fragmentation: %zu%%\n"
			 "Blocks: total=%zu free=%zu allocated=%zu\n",
			 stats.total_memory / 1024, stats.free_memory / 1024,
			 stats.allocated_memory / 1024,
			 stats.fragmentation_percent, stats.total_blocks,
			 stats.free_blocks, stats.allocated_blocks);
}

static const struct kernel_param_ops stats_param_ops = {
	.get = param_get_stats,
};

module_param_cb(stats, &stats_param_ops, NULL, 0444);
MODULE_PARM_DESC(stats, "Allocator statistics");

static int param_get_bitmap_info(char *buffer, const struct kernel_param *kp)
{
	return allocator_get_bitmap_info(buffer, PAGE_SIZE);
}

static const struct kernel_param_ops bitmap_info_param_ops = {
	.get = param_get_bitmap_info,
};

module_param_cb(bitmap_info, &bitmap_info_param_ops, NULL, 0444);
MODULE_PARM_DESC(bitmap_info, "Allocator bitmap state; X means allocated block");
