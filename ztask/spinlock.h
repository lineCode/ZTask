#ifndef _ZNET_SPINLOCK_H
#define _ZNET_SPINLOCK_H

#include "atomic.h"

#if !(defined(_WIN32) || defined(_WIN64))

#ifndef USE_THREAD_LOCK

typedef struct _spinlock {
    int lock;
}spinlock;

static inline void
spinlock_init(spinlock *lock) {
    lock->lock = 0;
}

static inline void
spinlock_lock(spinlock *lock) {
    while (__sync_lock_test_and_set(&lock->lock, 1)) {}
}

static inline int
spinlock_trylock(spinlock *lock) {
    return __sync_lock_test_and_set(&lock->lock, 1) == 0;
}

static inline void
spinlock_unlock(spinlock *lock) {
    __sync_lock_release(&lock->lock);
}

static inline void
spinlock_destroy(spinlock *lock) {
    (void)lock;
}

#define SPIN_INIT(q) spinlock_init(&(q)->lock);
#define SPIN_LOCK(q) spinlock_lock(&(q)->lock);
#define SPIN_UNLOCK(q) spinlock_unlock(&(q)->lock);
#define SPIN_DESTROY(q) spinlock_destroy(&(q)->lock);

#else

#include "thread.h"

// we use mutex instead of spinlock for some reason
// you can also replace to pthread_spinlock

typedef struct _spinlock {
    ztask_mutex lock;
}spinlock;

static inline void
spinlock_init(spinlock *lock) {
    ztask_mutex_init(&lock->lock);
}

static inline void
spinlock_lock(spinlock *lock) {
    ztask_mutex_lock(&lock->lock);
}

static inline int
spinlock_trylock(spinlock *lock) {
    return ztask_mutex_trylock(&lock->lock) == 0;
}

static inline void
spinlock_unlock(spinlock *lock) {
    ztask_mutex_unlock(&lock->lock);
}

static inline void
spinlock_destroy(spinlock *lock) {
    ztask_mutex_destroy(&lock->lock);
}

#endif
#else

#include <windows.h>
#define SPIN_INIT(q) spinlock_init(&(q)->lock);
#define SPIN_LOCK(q) spinlock_lock(&(q)->lock);
#define SPIN_UNLOCK(q) spinlock_unlock(&(q)->lock);
#define SPIN_DESTROY(q) spinlock_destroy(&(q)->lock);

typedef struct _spinlock {
    int lock;
}spinlock;

static inline void
spinlock_init(spinlock *lock) {
    lock->lock = 0;
}

static inline void
spinlock_lock(spinlock *lock) {
    //while (InterlockedExchange(&lock->lock, 1)) {}
    for (; 0 != InterlockedExchange(&lock->lock, 1);)
    {
        Sleep(1);
    }
}

static inline int
spinlock_trylock(spinlock *lock) {
    return InterlockedExchange(&lock->lock, 1) == 0;
}

static inline void
spinlock_unlock(spinlock *lock) {
    InterlockedExchange(&lock->lock, 0);
}

static inline void
spinlock_destroy(spinlock *lock) {
    (void)lock;
}


#endif
#endif
