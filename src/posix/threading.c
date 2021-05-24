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

// prevents multiple threads from dumping to I/O at once
static pthread_mutex_t _debug_lock = PTHREAD_MUTEX_INITIALIZER;

#define QD_THREAD_MAX_LOCKS   16  // simultaineously held locks
static __thread const char *_held_locks[QD_THREAD_MAX_LOCKS];
static __thread int _held_index;

static void _dump_locks()
{
    pthread_mutex_lock(&_debug_lock);
    for (int i = 0; i < _held_index; i++) {
        fprintf(stderr, "%s%s", i ? " ==> " : "LOCKS: ", _held_locks[i]);
    }
    fprintf(stderr, "\n");
    pthread_mutex_unlock(&_debug_lock);
}

#define TAKE_LOCK(N)                                \
    do {                                            \
        assert(_held_index < QD_THREAD_MAX_LOCKS);  \
        _held_locks[_held_index++] = (N);           \
        _dump_locks();                              \
    } while (0)

#define DROP_LOCK(N)                                    \
    do {                                                \
        assert(_held_index > 0);                        \
        const char *old = _held_locks[--_held_index];   \
        _held_locks[_held_index] = 0;                   \
        assert(strcmp((N), old) == 0);                  \
    } while (0);


#else

static void _dump_locks() {}
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
    int result = pthread_mutex_lock(&(mutex->mutex));
    _CHECK(result == 0);
    TAKE_LOCK(mutex->name);
    _dump_locks();
}


void sys_mutex_unlock(sys_mutex_t *mutex)
{
    DROP_LOCK(mutex->name);
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
    char *name;
    pthread_rwlock_t lock;
};


sys_rwlock_t *sys_rwlock(const char *name)
{
    sys_rwlock_t *lock = NEW(sys_rwlock_t);
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
    int result = pthread_rwlock_wrlock(&(lock->lock));
    assert(result == 0);
    TAKE_LOCK(lock->name);
    _dump_locks();
}


void sys_rwlock_rdlock(sys_rwlock_t *lock)
{
    int result = pthread_rwlock_rdlock(&(lock->lock));
    assert(result == 0);
    TAKE_LOCK(lock->name);
    _dump_locks();
}


void sys_rwlock_unlock(sys_rwlock_t *lock)
{
    DROP_LOCK(lock->name);
    int result = pthread_rwlock_unlock(&(lock->lock));
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
