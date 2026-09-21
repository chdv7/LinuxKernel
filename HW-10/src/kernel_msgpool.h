#ifndef SRC_KERNEL_MSGPOOL_H_
#define SRC_KERNEL_MSGPOOL_H_

#include <linux/atomic.h>
#include <linux/ktime.h>
#include <linux/mempool.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/types.h>

#define MSG_TEXT_MAX 128U
#define MSG_QUEUE_MAX 16U

#define MSGPOOL_DEFAULT_ALLOC_TYPE 0U
#define MSGPOOL_DEFAULT_POOL_MIN_NR 8U
#define MSGPOOL_DEFAULT_INTERVAL_MS 1000U

#define MSGPOOL_MIN_POOL_MIN_NR 1U
#define MSGPOOL_MAX_POOL_MIN_NR 64U
#define MSGPOOL_MIN_INTERVAL_MS 100U
#define MSGPOOL_MAX_INTERVAL_MS 60000U

#define MSGPOOL_ALLOC_CACHE 0U
#define MSGPOOL_ALLOC_MEMPOOL 1U

#define MP_OK 0
#define MP_INVALID (-EINVAL)
#define MP_NOMEM (-ENOMEM)
#define MP_BUSY (-EBUSY)
#define MP_FULL (-ENOBUFS)

struct msg {
	char text[MSG_TEXT_MAX];
	ktime_t enqueue_time;
	unsigned int seq;
};

struct msg_queue {
	struct msg *slots[MSG_QUEUE_MAX];
	unsigned int head;
	unsigned int tail;
	unsigned int count;
	spinlock_t lock;
};

struct msgpool_ctx {
	unsigned int alloc_type;
	unsigned int pool_min_nr;
	unsigned int interval_ms;

	struct kmem_cache *msg_cache;
	mempool_t *msg_pool;

	struct msg_queue queue;
	struct timer_list consumer_timer;

	atomic_t sent_total;
	atomic_t consumed_total;
	atomic_t flushed_total;
	atomic_t dropped_total;

	char last_msg[MSG_TEXT_MAX];
	spinlock_t last_msg_lock;

	unsigned int seq_counter;
	struct mutex control_lock;
	bool initialized;
};

extern struct msgpool_ctx msgpool_context;

bool msgpool_alloc_type_valid(unsigned int value);
bool msgpool_pool_min_nr_valid(unsigned int value);
bool msgpool_interval_valid(unsigned int value);
const char *msgpool_alloc_name(unsigned int alloc_type);

int msg_allocator_init(struct msgpool_ctx *ctx);
void msg_allocator_destroy(struct msgpool_ctx *ctx);
struct msg *msg_alloc(struct msgpool_ctx *ctx, unsigned int alloc_type,
		      gfp_t gfp);
void msg_free(struct msgpool_ctx *ctx, struct msg *obj,
	      unsigned int alloc_type);

void msg_queue_init(struct msg_queue *queue);
int msg_queue_enqueue(struct msg_queue *queue, struct msg *obj);
unsigned int msg_queue_drain(struct msgpool_ctx *ctx, struct msg **items,
			     unsigned int max_items,
			     unsigned int *alloc_type);
unsigned int msg_queue_count(struct msg_queue *queue);
bool msg_queue_empty(struct msg_queue *queue);
unsigned int msgpool_flush_pending(struct msgpool_ctx *ctx, bool count_flush);

void msgpool_consumer_timer(struct timer_list *timer);

#endif /* SRC_KERNEL_MSGPOOL_H_ */
