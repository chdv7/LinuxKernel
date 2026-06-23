#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitmap.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#include "kernel_alloc.h"

struct memory_allocator {
	DECLARE_BITMAP(bitmap, ALLOCATOR_TOTAL_BLOCKS);
	u16 allocations[ALLOCATOR_TOTAL_BLOCKS];
	void *memory_pool;
	spinlock_t lock;
};

static struct memory_allocator allocator;

static size_t allocator_largest_free_run(void)
{
	size_t current_run = 0;
	size_t largest_run = 0;
	size_t i;

	for (i = 0; i < ALLOCATOR_TOTAL_BLOCKS; ++i) {
		if (!test_bit(i, allocator.bitmap)) {
			++current_run;
			if (current_run > largest_run)
				largest_run = current_run;
		} else {
			current_run = 0;
		}
	}

	return largest_run;
}

int allocator_init(void)
{
	spin_lock_init(&allocator.lock);
	bitmap_zero(allocator.bitmap, ALLOCATOR_TOTAL_BLOCKS);
	memset(allocator.allocations, 0, sizeof(allocator.allocations));

	allocator.memory_pool = vmalloc(ALLOCATOR_TOTAL_MEMORY);
	if (!allocator.memory_pool)
		return ALLOC_NOMEM;

	pr_info("allocator initialized: total=%lu bytes block=%lu bytes blocks=%lu\n",
		ALLOCATOR_TOTAL_MEMORY, ALLOCATOR_BLOCK_SIZE,
		ALLOCATOR_TOTAL_BLOCKS);

	return ALLOC_OK;
}

void allocator_cleanup(void)
{
	unsigned long flags;
	void *memory_pool;

	spin_lock_irqsave(&allocator.lock, flags);
	memory_pool = allocator.memory_pool;
	allocator.memory_pool = NULL;
	bitmap_zero(allocator.bitmap, ALLOCATOR_TOTAL_BLOCKS);
	memset(allocator.allocations, 0, sizeof(allocator.allocations));
	spin_unlock_irqrestore(&allocator.lock, flags);

	if (memory_pool)
		vfree(memory_pool);

	pr_info("allocator cleaned up\n");
}

void *allocator_alloc(size_t bytes)
{
	unsigned long flags;
	unsigned long start_block;
	size_t num_blocks;
	void *ptr;

	if (!allocator.memory_pool || bytes == 0 || bytes > ALLOCATOR_TOTAL_MEMORY)
		return NULL;

	num_blocks = DIV_ROUND_UP(bytes, ALLOCATOR_BLOCK_SIZE);
	if (num_blocks > ALLOCATOR_TOTAL_BLOCKS)
		return NULL;

	spin_lock_irqsave(&allocator.lock, flags);

	start_block = bitmap_find_next_zero_area(allocator.bitmap,
						   ALLOCATOR_TOTAL_BLOCKS, 0,
						   num_blocks, 0);
	if (start_block >= ALLOCATOR_TOTAL_BLOCKS) {
		spin_unlock_irqrestore(&allocator.lock, flags);
		return NULL;
	}

	bitmap_set(allocator.bitmap, start_block, num_blocks);
	allocator.allocations[start_block] = (u16)num_blocks;
	ptr = (char *)allocator.memory_pool + start_block * ALLOCATOR_BLOCK_SIZE;

	spin_unlock_irqrestore(&allocator.lock, flags);

	return ptr;
}

int allocator_free(void *ptr)
{
	unsigned long addr;
	unsigned long flags;
	unsigned long offset;
	unsigned long pool_start;
	unsigned long pool_end;
	unsigned long start_block;
	u16 num_blocks;

	if (!ptr || !allocator.memory_pool)
		return ALLOC_INVALID;

	addr = (unsigned long)ptr;
	pool_start = (unsigned long)allocator.memory_pool;
	pool_end = pool_start + ALLOCATOR_TOTAL_MEMORY;

	if (addr < pool_start || addr >= pool_end)
		return ALLOC_INVALID;

	offset = addr - pool_start;
	if (offset % ALLOCATOR_BLOCK_SIZE != 0)
		return ALLOC_INVALID;

	start_block = offset / ALLOCATOR_BLOCK_SIZE;
	if (start_block >= ALLOCATOR_TOTAL_BLOCKS)
		return ALLOC_INVALID;

	spin_lock_irqsave(&allocator.lock, flags);

	num_blocks = allocator.allocations[start_block];
	if (num_blocks == 0) {
		spin_unlock_irqrestore(&allocator.lock, flags);
		return ALLOC_NOT_FOUND;
	}

	bitmap_clear(allocator.bitmap, start_block, num_blocks);
	allocator.allocations[start_block] = 0;

	spin_unlock_irqrestore(&allocator.lock, flags);

	return ALLOC_OK;
}

struct stats_info allocator_get_stats(void)
{
	unsigned long flags;
	size_t free_blocks;
	size_t largest_free_run;
	unsigned int allocated_blocks;
	struct stats_info stats = { 0 };

	if (!allocator.memory_pool)
		return stats;

	spin_lock_irqsave(&allocator.lock, flags);

	allocated_blocks = bitmap_weight(allocator.bitmap, ALLOCATOR_TOTAL_BLOCKS);
	free_blocks = ALLOCATOR_TOTAL_BLOCKS - allocated_blocks;
	largest_free_run = allocator_largest_free_run();

	stats.total_blocks = ALLOCATOR_TOTAL_BLOCKS;
	stats.free_blocks = free_blocks;
	stats.allocated_blocks = allocated_blocks;
	stats.total_memory = ALLOCATOR_TOTAL_MEMORY;
	stats.free_memory = free_blocks * ALLOCATOR_BLOCK_SIZE;
	stats.allocated_memory = allocated_blocks * ALLOCATOR_BLOCK_SIZE;
	if (free_blocks == 0 || largest_free_run == free_blocks)
		stats.fragmentation_percent = 0;
	else
		stats.fragmentation_percent =
			((free_blocks - largest_free_run) * 100) / free_blocks;

	spin_unlock_irqrestore(&allocator.lock, flags);

	return stats;
}

int allocator_get_bitmap_info(char *buffer, size_t size)
{
	unsigned long flags;
	size_t i;
	int len = 0;

	if (!buffer || size == 0)
		return -EINVAL;

	if (!allocator.memory_pool)
		return scnprintf(buffer, size, "allocator is not initialized\n");

	spin_lock_irqsave(&allocator.lock, flags);

	len += scnprintf(buffer + len, size - len, "[");
	for (i = 0; i < ALLOCATOR_TOTAL_BLOCKS && len < size - 2; ++i) {
		len += scnprintf(buffer + len, size - len, "%c",
				 test_bit(i, allocator.bitmap) ? 'X' : '.');

		if ((i + 1) % 80 == 0 && len < size - 2)
			len += scnprintf(buffer + len, size - len, "\n");
	}

	if (len < size - 1)
		len += scnprintf(buffer + len, size - len, "]\n");

	spin_unlock_irqrestore(&allocator.lock, flags);

	return len;
}
