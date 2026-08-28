#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>

#include "kernel_sync.h"

bool sync_num_threads_valid(unsigned int value)
{
	return value >= SYNC_MIN_THREADS && value <= SYNC_MAX_THREADS;
}

bool sync_iterations_valid(unsigned int value)
{
	return value >= SYNC_MIN_ITERATIONS && value <= SYNC_MAX_ITERATIONS;
}

bool sync_lock_type_valid(unsigned int value)
{
	return value == SYNC_LOCK_SPINLOCK || value == SYNC_LOCK_MUTEX ||
	       value == SYNC_LOCK_SEMAPHORE;
}

const char *sync_lock_name(unsigned int lock_type)
{
	switch (lock_type) {
	case SYNC_LOCK_SPINLOCK:
		return "spinlock";
	case SYNC_LOCK_MUTEX:
		return "mutex";
	case SYNC_LOCK_SEMAPHORE:
		return "semaphore";
	default:
		return "unknown";
	}
}

void sync_lock_init(struct sync_ctx *ctx)
{
	spin_lock_init(&ctx->slock);
	mutex_init(&ctx->mlock);
	sema_init(&ctx->sem, 1);
}

void sync_lock_acquire(struct sync_ctx *ctx)
{
	switch (ctx->lock_type) {
	case SYNC_LOCK_SPINLOCK:
		spin_lock(&ctx->slock);
		break;
	case SYNC_LOCK_MUTEX:
		mutex_lock(&ctx->mlock);
		break;
	case SYNC_LOCK_SEMAPHORE:
		down(&ctx->sem);
		break;
	}
}

void sync_lock_release(struct sync_ctx *ctx)
{
	switch (ctx->lock_type) {
	case SYNC_LOCK_SPINLOCK:
		spin_unlock(&ctx->slock);
		break;
	case SYNC_LOCK_MUTEX:
		mutex_unlock(&ctx->mlock);
		break;
	case SYNC_LOCK_SEMAPHORE:
		up(&ctx->sem);
		break;
	}
}
