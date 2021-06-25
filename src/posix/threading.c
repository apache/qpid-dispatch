/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */


#include "qpid/dispatch/threading.h"
#include "qpid/dispatch/timer.h"

#include "qpid/dispatch/ctools.h"

#include "aprintf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <inttypes.h>

typedef struct sys_mutex_debug_s sys_mutex_debug_t;

struct sys_mutex_debug_s {
    char               *name;
    void               *sys_mutex;
    qd_timestamp_us_t   acquire_start;
    qd_timestamp_us_t   acquire_granted;
    qd_timestamp_us_t   acquire_released;
};

/*
 * Thread and mutex operations are critical. If any of these operations fail
 * then the system is in an undefined state. On failure abort the system.
 */
#define _CHECK(B) \
    if (!(B)) {                                                 \
        fprintf(stderr, "%s:%s:%i: " #B " failed: %s\n",        \
                __FILE__, __func__, __LINE__, strerror(errno)); \
        fflush(stderr);                                         \
        abort();                                                \
    }


#ifdef QD_THREAD_DEBUG

// competing _dump functions may print timestamped lines out of order

#define QD_THREAD_MAX_LOCKS   16  // simultaineously held locks
#define PBUFSIZE 1000             // print buffer size
static __thread sys_mutex_debug_t _held_locks[QD_THREAD_MAX_LOCKS];
static __thread int _held_index;
static char *PHONY_IDLE_LOCK_NAME = "CORE_IDLE";

static void _dump_locks_lock()
{
    char buffer[PBUFSIZE];
    char *bufptr = buffer;
    char *end = &buffer[PBUFSIZE];
    const sys_mutex_debug_t *last_locked = &_held_locks[_held_index - 1];
    aprintf(&bufptr, end, "%"PRIu64" TID: %14p ", last_locked->acquire_start, (void*)sys_thread_self());
    for (int i = 0; i < _held_index; i++) {
        aprintf(&bufptr, end, "%s%16s", i ? " " : "  LOCK ", _held_locks[i].name);
    }
    aprintf(&bufptr, end, " ==> [ LOCK acquire_uS %ld ]\n", last_locked->acquire_granted - last_locked->acquire_start);
    fprintf(stderr, "%s", buffer);
}

static void _dump_locks_unlock()
{
    char buffer[PBUFSIZE];
    char *bufptr = buffer;
    char *end = &buffer[PBUFSIZE];
    const sys_mutex_debug_t *last_locked = &_held_locks[_held_index - 1];
    aprintf(&bufptr, end, "%"PRIu64" TID: %14p ", last_locked->acquire_released, (void*)sys_thread_self());
    for (int i = 0; i < _held_index; i++) {
        aprintf(&bufptr, end, "%s%16s", i ? " " : "UNLOCK ", _held_locks[i].name);
    }
    qd_timestamp_us_t acquire = last_locked->acquire_granted  - last_locked->acquire_start;
    qd_timestamp_us_t use     = last_locked->acquire_released - last_locked->acquire_granted;
    qd_timestamp_us_t total   = last_locked->acquire_released - last_locked->acquire_start;
    assert(total < 1000000000);
    aprintf(&bufptr, end, " <== [%p UNLOCK acquire_uS %ld use %ld total  %ld ]\n", last_locked->sys_mutex, acquire, use, total);
    fprintf(stderr, "%s", buffer);
}

static char *_current_lock_name()
{
    return _held_locks[_held_index - 1].name;
}


#define TAKE_LOCK(N, L, A_START, A_GRANTED)                      \
    do {                                                      \
        assert(_held_index < QD_THREAD_MAX_LOCKS);            \
        sys_mutex_debug_t * mtxd = &_held_locks[_held_index]; \
        mtxd->name = (N);                                     \
        mtxd->sys_mutex = (L);                                \
        mtxd->acquire_start   = (A_START);                    \
        mtxd->acquire_granted = (A_GRANTED);                  \
        _held_index++;                                        \
        _dump_locks_lock();                                   \
    } while (0)

#define DROP_LOCK(N, A_RELEASED)                                \
    do {                                                        \
        assert(_held_index > 0);                                \
        sys_mutex_debug_t * mtxd = &_held_locks[_held_index-1]; \
        assert(strcmp((N), mtxd->name) == 0);                   \
        mtxd->acquire_released = (A_RELEASED);                  \
        _dump_locks_unlock();                                   \
        mtxd->name = 0;                                         \
        mtxd->acquire_start = 0;                                \
        mtxd->acquire_granted = 0;                              \
        mtxd->acquire_released = 0;                             \
        _held_index--;                                          \
    } while (0);

#else

static void _dump_locks_lock() {}
static void _dump_locks_unlock() {}
#define TAKE_LOCK(M)
#define DROP_LOCK(M)

#endif


struct sys_mutex_t {
    char *name;
    pthread_mutex_t mutex;
};


sys_mutex_t *sys_mutex(const char *name)
{
    sys_mutex_t *mutex = 0;
    NEW_CACHE_ALIGNED(sys_mutex_t, mutex);
    _CHECK(mutex != 0);
    ZERO(mutex);
    mutex->name = qd_strdup(name);
    int result = pthread_mutex_init(&(mutex->mutex), 0);
    _CHECK(result == 0);
    return mutex;
}


void sys_mutex_free(sys_mutex_t *mutex)
{
    int result = pthread_mutex_destroy(&(mutex->mutex));
    _CHECK(result == 0);
    free(mutex->name);
    free(mutex);
}


void sys_mutex_lock(sys_mutex_t *mutex)
{
    qd_timestamp_us_t acquire_start = qd_timer_us_now();
    int result = pthread_mutex_lock(&(mutex->mutex));
    qd_timestamp_us_t acquire_granted = qd_timer_us_now();
    _CHECK(result == 0);
    TAKE_LOCK(mutex->name, (void*)&(mutex->mutex), acquire_start, acquire_granted);
}


void sys_mutex_unlock(sys_mutex_t *mutex)
{
    qd_timestamp_us_t gonna_release = qd_timer_us_now();
    int result = pthread_mutex_unlock(&(mutex->mutex));
    qd_timestamp_us_t acquire_released = qd_timer_us_now();
    assert(acquire_released >= gonna_release);
    DROP_LOCK(mutex->name, acquire_released);
    _CHECK(result == 0);
}


struct sys_cond_t {
    pthread_cond_t cond;
};


sys_cond_t *sys_cond(void)
{
    sys_cond_t *cond = 0;
    NEW_CACHE_ALIGNED(sys_cond_t, cond);
    _CHECK(cond != 0);
    int result = pthread_cond_init(&(cond->cond), 0);
    _CHECK(result == 0);
    return cond;
}


void sys_cond_free(sys_cond_t *cond)
{
    int result = pthread_cond_destroy(&(cond->cond));
    _CHECK(result == 0);
    free(cond);
}


void sys_cond_wait(sys_cond_t *cond, sys_mutex_t *held_mutex)
{
    char *held_mutex_name = _current_lock_name();
    qd_timestamp_us_t now = qd_timer_us_now();
    DROP_LOCK(held_mutex_name, now);
    TAKE_LOCK(PHONY_IDLE_LOCK_NAME, (void*)1, now, now);
    int result = pthread_cond_wait(&(cond->cond), &(held_mutex->mutex));
    now = qd_timer_us_now();
    DROP_LOCK(PHONY_IDLE_LOCK_NAME, now);
    TAKE_LOCK(held_mutex_name, 0, now, now);
    _CHECK(result == 0);
}


void sys_cond_signal(sys_cond_t *cond)
{
    int result = pthread_cond_signal(&(cond->cond));
    _CHECK(result == 0);
}


void sys_cond_signal_all(sys_cond_t *cond)
{
    int result = pthread_cond_broadcast(&(cond->cond));
    _CHECK(result == 0);
}


struct sys_rwlock_t {
    char *name;
    pthread_rwlock_t lock;
};


sys_rwlock_t *sys_rwlock(const char *name)
{
    sys_rwlock_t *lock = NEW(sys_rwlock_t);
    ZERO(lock);
    lock->name = qd_strdup(name);
    int result = pthread_rwlock_init(&(lock->lock), 0);
    _CHECK(result == 0);
    return lock;
}


void sys_rwlock_free(sys_rwlock_t *lock)
{
    int result = pthread_rwlock_destroy(&(lock->lock));
    _CHECK(result == 0);
    free(lock->name);
    free(lock);
}


void sys_rwlock_wrlock(sys_rwlock_t *lock)
{
    qd_timestamp_us_t acquire_start = qd_timer_us_now();
    int result = pthread_rwlock_wrlock(&(lock->lock));
    qd_timestamp_us_t acquire_granted = qd_timer_us_now();
    assert(result == 0);
    TAKE_LOCK(lock->name, (void*)&(lock->lock), acquire_start, acquire_granted);
}


void sys_rwlock_rdlock(sys_rwlock_t *lock)
{
    qd_timestamp_us_t acquire_start = qd_timer_us_now();
    int result = pthread_rwlock_rdlock(&(lock->lock));
    qd_timestamp_us_t acquire_granted = qd_timer_us_now();
    assert(result == 0);
    TAKE_LOCK(lock->name, (void*)&(lock->lock), acquire_start, acquire_granted);
}


void sys_rwlock_unlock(sys_rwlock_t *lock)
{
    qd_timestamp_us_t gonna_release = qd_timer_us_now();
    int result = pthread_rwlock_unlock(&(lock->lock));
    qd_timestamp_us_t acquire_released = qd_timer_us_now();
    assert(acquire_released >= gonna_release);
    DROP_LOCK(lock->name, acquire_released);
    assert(result == 0);
}


struct sys_thread_t {
    pthread_t thread;
    void *(*f)(void *);
    void *arg;
};

// initialize the per-thread _self to a non-zero value.  This dummy value will
// be returned when sys_thread_self() is called from the process's main thread
// of execution (which is not a pthread).  Using a non-zero value provides a
// way to distinguish a thread id from a zero (unset) value.
//
static sys_thread_t  _main_thread_id;
static __thread sys_thread_t *_self = &_main_thread_id;


// bootstrap _self before calling thread's main function
//
static void *_thread_init(void *arg)
{
    _self = (sys_thread_t*) arg;
    return _self->f(_self->arg);
}


sys_thread_t *sys_thread(void *(*run_function) (void *), void *arg)
{
    sys_thread_t *thread = NEW(sys_thread_t);
    thread->f = run_function;
    thread->arg = arg;
    pthread_create(&(thread->thread), 0, _thread_init, (void*) thread);
    return thread;
}


sys_thread_t *sys_thread_self()
{
    return _self;
}


void sys_thread_free(sys_thread_t *thread)
{
    assert(thread != &_main_thread_id);
    free(thread);
}


void sys_thread_join(sys_thread_t *thread)
{
    assert(thread != &_main_thread_id);
    pthread_join(thread->thread, 0);
}
