#ifndef SRC_KERNEL_SYNC_H_
#define SRC_KERNEL_SYNC_H_

#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/sched.h>

#define SYNC_DEFAULT_NUM_THREADS 4U
#define SYNC_DEFAULT_ITERATIONS 1000U
#define SYNC_DEFAULT_LOCK_TYPE 0U

#define SYNC_MIN_THREADS 1U
#define SYNC_MAX_THREADS 32U
#define SYNC_MIN_ITERATIONS 1U
#define SYNC_MAX_ITERATIONS 1000000U

#define SYNC_LOCK_SPINLOCK 0U
#define SYNC_LOCK_MUTEX 1U
#define SYNC_LOCK_SEMAPHORE 2U

#define SYNC_WAIT_THRESHOLD_NS 100LL

#define SD_OK 0
#define SD_INVALID -EINVAL
#define SD_NOMEM -ENOMEM
#define SD_BUSY -EBUSY

struct sync_ctx {
	unsigned int num_threads;
	unsigned int iterations;
	unsigned int lock_type;

	long long shared_counter;

	spinlock_t slock;
	struct mutex mlock;
	struct semaphore sem;

	ktime_t total_wait_time;
	unsigned int contention_count;

	struct task_struct **threads;
	atomic_t threads_done;
	int last_run_result;
};

struct worker_args {
	struct sync_ctx *ctx;
	unsigned int thread_id;
	ktime_t wait_time;
	unsigned int contention_count;
};

extern struct sync_ctx sync_context;
extern struct mutex sync_control_mutex;

bool sync_num_threads_valid(unsigned int value);
bool sync_iterations_valid(unsigned int value);
bool sync_lock_type_valid(unsigned int value);
const char *sync_lock_name(unsigned int lock_type);

void sync_lock_init(struct sync_ctx *ctx);
void sync_lock_acquire(struct sync_ctx *ctx);
void sync_lock_release(struct sync_ctx *ctx);

int sync_run_test(struct sync_ctx *ctx);

#endif /* SRC_KERNEL_SYNC_H_ */
