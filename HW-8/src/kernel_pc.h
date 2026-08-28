#ifndef SRC_KERNEL_PC_H_
#define SRC_KERNEL_PC_H_

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#define PC_DEFAULT_FIFO_SIZE 64U
#define PC_DEFAULT_NUM_EVENTS 200U
#define PC_DEFAULT_INTERVAL_US 1000U
#define PC_DEFAULT_CONSUMER_TYPE 0U

#define PC_MIN_FIFO_SIZE 4U
#define PC_MAX_FIFO_SIZE 1024U
#define PC_MIN_NUM_EVENTS 1U
#define PC_MAX_NUM_EVENTS 50000U
#define PC_MIN_INTERVAL_US 100U
#define PC_MAX_INTERVAL_US 1000000U

#define PC_CONSUMER_TASKLET 0U
#define PC_CONSUMER_WORKQUEUE 1U

#define PC_OK 0
#define PC_INVALID -EINVAL
#define PC_NOMEM -ENOMEM
#define PC_BUSY -EBUSY
#define PC_TIMEOUT -ETIMEDOUT

struct pc_ctx {
	unsigned int fifo_size;
	unsigned int num_events;
	unsigned int interval_us;
	unsigned int consumer_type;

	DECLARE_KFIFO_PTR(fifo, unsigned int);

	struct hrtimer timer;
	atomic_t produced;
	atomic_t dropped;

	struct tasklet_struct tasklet;

	struct workqueue_struct *wq;
	struct work_struct work;

	atomic_t consumed;
	u64 sum;
	unsigned int last_value;
	struct mutex stats_lock;

	wait_queue_head_t producer_done_waitq;
	atomic_t stopping;
	bool active;
	bool tasklet_initialized;

	unsigned int last_run_consumer_type;
	int last_run_result;
};

extern struct pc_ctx pc_context;
extern struct mutex pc_control_mutex;
extern bool pc_module_ready;

bool pc_fifo_size_valid(unsigned int value);
bool pc_num_events_valid(unsigned int value);
bool pc_interval_us_valid(unsigned int value);
bool pc_consumer_type_valid(unsigned int value);
const char *pc_consumer_name(unsigned int consumer_type);

void pc_clear_run_state(struct pc_ctx *ctx);
int pc_run_test(struct pc_ctx *ctx);

int pc_consumer_start(struct pc_ctx *ctx);
void pc_consumer_schedule(struct pc_ctx *ctx);
void pc_consumer_stop(struct pc_ctx *ctx, bool drain);
void tasklet_consumer(unsigned long data);
void work_consumer(struct work_struct *work);

enum hrtimer_restart pc_timer_callback(struct hrtimer *timer);

#endif /* SRC_KERNEL_PC_H_ */
