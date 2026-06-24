#ifndef SRC_KERNEL_ALLOC_H_
#define SRC_KERNEL_ALLOC_H_

#include <linux/types.h>

#define ALLOCATOR_TOTAL_MEMORY (10 * 1024 * 1024UL)
#define ALLOCATOR_BLOCK_SIZE 4096UL
#define ALLOCATOR_TOTAL_BLOCKS (ALLOCATOR_TOTAL_MEMORY / ALLOCATOR_BLOCK_SIZE)

#define ALLOC_OK 0
#define ALLOC_NOMEM -1
#define ALLOC_INVALID -2
#define ALLOC_NOT_FOUND -3

struct stats_info {
	size_t total_blocks;
	size_t free_blocks;
	size_t allocated_blocks;
	size_t total_memory;
	size_t free_memory;
	size_t allocated_memory;
	size_t fragmentation_percent;
};

int allocator_init(void);
void allocator_cleanup(void);
void *allocator_alloc(size_t bytes);
int allocator_free(void *ptr);
struct stats_info allocator_get_stats(void);
int allocator_get_bitmap_info(char *buffer, size_t size);

#endif /* SRC_KERNEL_ALLOC_H_ */
