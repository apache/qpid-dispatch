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

#include "qpid/dispatch/ctools.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <inttypes.h>

#ifndef CLOCK_MONOTONIC_RAW   // linux-specific
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

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


#ifdef QD_TRACE_LOCK_CONTENTION
static inline uint64_t timediff(const struct timespec start,
                                const struct timespec stop)
{
    uint64_t nsecs = (stop.tv_sec - start.tv_sec) * 1000000000UL;
    return stop.tv_nsec + nsecs - start.tv_nsec;
}

#define TRACE_INIT(M, N)                        \
    do {                                        \
        (M)->name = qd_strdup((N));             \
        (M)->nsec_wait = 0;                     \
        (M)->lock_count = 0;                    \
        (M)->max_wait[0] = 0;                   \
        (M)->max_wait[1] = 0;                   \
        (M)->max_wait[2] = 0;                   \
    } while (0);

#define TRACE_DUMP(M)                                                   \
    do {                                                                \
        if ((M)->lock_count) {                                          \
            fprintf(stderr,                                             \
                    "%30s: total(nsec)= %-10"PRIu64                     \
                    " avg= %-10"PRIu64" count= %-6"PRIu64               \
                    " max= %"PRIu64" %"PRIu64" %"PRIu64"\n",            \
                    (M)->name, (M)->nsec_wait,                          \
                    (M)->nsec_wait/(M)->lock_count, (M)->lock_count,    \
                    (M)->max_wait[0], (M)->max_wait[1],                 \
                    (M)->max_wait[2]);                                  \
        }                                                               \
        free((M)->name);                                                \
    } while (0)

#define TRACE_UPDATE(M,N)                                        \
    if ((N) > 1000) {                                            \
        /* ignore short delays: normal overhead */               \
        (M)->lock_count += 1;                                    \
        (M)->nsec_wait += (N);                                   \
        if ((M)->lock_count == 1) {                              \
            /* do not use first lock attempt: */                 \
            /* It is expensive due to initial setup */           \
        } else if ((N) > (M)->max_wait[0]) {                     \
            (M)->max_wait[2] = (M)->max_wait[1];                 \
            (M)->max_wait[1] = (M)->max_wait[0];                 \
            (M)->max_wait[0] = (N);                              \
        } else if ((N) > (M)->max_wait[1]) {                     \
            (M)->max_wait[2] = (M)->max_wait[1];                 \
            (M)->max_wait[1] = (N);                              \
        } else if ((N) > (M)->max_wait[2]) {                     \
            (M)->max_wait[2] = (N);                              \
        }                                                        \
    }

#else
#define TRACE_INIT(M, N) (void)(N)
#define TRACE_DUMP(M)
#endif


struct sys_mutex_t {
    pthread_mutex_t mutex;
#ifdef QD_TRACE_LOCK_CONTENTION
    char *name;
    uint64_t nsec_wait;
    uint64_t lock_count;
    uint64_t max_wait[3];
#endif
};


sys_mutex_t *sys_mutex(const char *name)
{
    sys_mutex_t *mutex = 0;
    NEW_CACHE_ALIGNED(sys_mutex_t, mutex);
    _CHECK(mutex != 0);
    int result = pthread_mutex_init(&(mutex->mutex), 0);
    _CHECK(result == 0);
    TRACE_INIT(mutex, name);
    return mutex;
}


void sys_mutex_free(sys_mutex_t *mutex)
{
    int result = pthread_mutex_destroy(&(mutex->mutex));
    _CHECK(result == 0);
    TRACE_DUMP(mutex);
    FREE_CACHE_ALIGNED(mutex);
}


void sys_mutex_lock(sys_mutex_t *mutex)
{
#ifdef QD_TRACE_LOCK_CONTENTION
    struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
#endif
    const int result = pthread_mutex_lock(&(mutex->mutex));
#ifdef QD_TRACE_LOCK_CONTENTION
    clock_gettime(CLOCK_MONOTONIC_RAW, &stop);
    uint64_t nsec = timediff(start, stop);
    TRACE_UPDATE(mutex, nsec);
#endif
    _CHECK(result == 0);

}


void sys_mutex_unlock(sys_mutex_t *mutex)
{
    int result = pthread_mutex_unlock(&(mutex->mutex));
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
    int result = pthread_cond_wait(&(cond->cond), &(held_mutex->mutex));
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
    pthread_rwlock_t lock;
#ifdef QD_TRACE_LOCK_CONTENTION
    char *name;
    uint64_t nsec_wait;
    uint64_t lock_count;
    uint64_t max_wait[3];
#endif
};


sys_rwlock_t *sys_rwlock(const char *name)
{
    sys_rwlock_t *lock = NEW(sys_rwlock_t);
    int result = pthread_rwlock_init(&(lock->lock), 0);
    _CHECK(result == 0);
    TRACE_INIT(lock, name);
    return lock;
}


void sys_rwlock_free(sys_rwlock_t *lock)
{
    int result = pthread_rwlock_destroy(&(lock->lock));
    _CHECK(result == 0);
    TRACE_DUMP(lock);
    free(lock);
}


void sys_rwlock_wrlock(sys_rwlock_t *lock)
{
#ifdef QD_TRACE_LOCK_CONTENTION
    struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
#endif
    const int result = pthread_rwlock_wrlock(&(lock->lock));
#ifdef QD_TRACE_LOCK_CONTENTION
    clock_gettime(CLOCK_MONOTONIC_RAW, &stop);
    uint64_t nsec = timediff(start, stop);
    TRACE_UPDATE(lock, nsec);
#endif
    _CHECK(result == 0);
}


void sys_rwlock_rdlock(sys_rwlock_t *lock)
{
#ifdef QD_TRACE_LOCK_CONTENTION
    struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
#endif
    int result = pthread_rwlock_rdlock(&(lock->lock));
#ifdef QD_TRACE_LOCK_CONTENTION
    clock_gettime(CLOCK_MONOTONIC_RAW, &stop);
    uint64_t nsec = timediff(start, stop);
    TRACE_UPDATE(lock, nsec);
#endif
    _CHECK(result == 0);
}


void sys_rwlock_unlock(sys_rwlock_t *lock)
{
    int result = pthread_rwlock_unlock(&(lock->lock));
    _CHECK(result == 0);
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
