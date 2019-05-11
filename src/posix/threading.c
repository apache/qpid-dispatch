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

//
// Enable debug for asserts in this module regardless of what the project-wide
// setting is.
//
#undef NDEBUG

#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/ctools.h>
#include <stdio.h>
#include <pthread.h>
#include <assert.h>

struct sys_mutex_t {
    pthread_mutex_t mutex;
};


sys_mutex_t *sys_mutex(void)
{
    sys_mutex_t *mutex = 0;
    NEW_CACHE_ALIGNED(sys_mutex_t, mutex);
    assert(mutex != 0);
    pthread_mutex_init(&(mutex->mutex), 0);
    return mutex;
}


void sys_mutex_free(sys_mutex_t *mutex)
{
    pthread_mutex_destroy(&(mutex->mutex));
    free(mutex);
}


void sys_mutex_lock(sys_mutex_t *mutex)
{
    int result = pthread_mutex_lock(&(mutex->mutex));
    assert(result == 0);
}


void sys_mutex_unlock(sys_mutex_t *mutex)
{
    int result = pthread_mutex_unlock(&(mutex->mutex));
    assert(result == 0);
}

inline void sys_spin_destroy(sys_spinlock_t *const lock)
{
    int ret = pthread_spin_destroy(lock);
    assert(ret == 0);
}

inline void sys_spin_init(sys_spinlock_t *const lock)
{
    int ret = pthread_spin_init(lock, 0);
    assert(ret == 0);
}

inline bool sys_spin_trylock(sys_spinlock_t *const lock)
{
    const int ret = pthread_spin_trylock(lock);
    const bool success = ret == 0 ? true : false;
    return success;
}

inline void sys_spin_lock(sys_spinlock_t *const lock)
{
    const int ret = pthread_spin_lock(lock);
    assert(ret == 0);
}

inline void sys_spin_unlock(sys_spinlock_t *const lock)
{
    const int ret = pthread_spin_unlock(lock);
    assert(ret == 0);
}

struct sys_cond_t {
    pthread_cond_t cond;
};


sys_cond_t *sys_cond(void)
{
    sys_cond_t *cond = 0;
    NEW_CACHE_ALIGNED(sys_cond_t, cond);
    pthread_cond_init(&(cond->cond), 0);
    return cond;
}


void sys_cond_free(sys_cond_t *cond)
{
    pthread_cond_destroy(&(cond->cond));
    free(cond);
}


void sys_cond_wait(sys_cond_t *cond, sys_mutex_t *held_mutex)
{
    int result = pthread_cond_wait(&(cond->cond), &(held_mutex->mutex));
    assert(result == 0);
}


void sys_cond_signal(sys_cond_t *cond)
{
    int result = pthread_cond_signal(&(cond->cond));
    assert(result == 0);
}


void sys_cond_signal_all(sys_cond_t *cond)
{
    int result = pthread_cond_broadcast(&(cond->cond));
    assert(result == 0);
}


struct sys_rwlock_t {
    pthread_rwlock_t lock;
};


sys_rwlock_t *sys_rwlock(void)
{
    sys_rwlock_t *lock = NEW(sys_rwlock_t);
    pthread_rwlock_init(&(lock->lock), 0);
    return lock;
}


void sys_rwlock_free(sys_rwlock_t *lock)
{
    pthread_rwlock_destroy(&(lock->lock));
    free(lock);
}


void sys_rwlock_wrlock(sys_rwlock_t *lock)
{
    int result = pthread_rwlock_wrlock(&(lock->lock));
    assert(result == 0);
}


void sys_rwlock_rdlock(sys_rwlock_t *lock)
{
    int result = pthread_rwlock_rdlock(&(lock->lock));
    assert(result == 0);
}


void sys_rwlock_unlock(sys_rwlock_t *lock)
{
    int result = pthread_rwlock_unlock(&(lock->lock));
    assert(result == 0);
}


struct sys_thread_t {
    pthread_t thread;
};

sys_thread_t *sys_thread(void *(*run_function) (void *), void *arg)
{
    sys_thread_t *thread = NEW(sys_thread_t);
    pthread_create(&(thread->thread), 0, run_function, arg);
    return thread;
}

long sys_thread_id(sys_thread_t *thread) {
    return (long) thread->thread;
}

long sys_thread_self() {
    return pthread_self();
}

void sys_thread_free(sys_thread_t *thread)
{
    free(thread);
}


void sys_thread_join(sys_thread_t *thread)
{
    pthread_join(thread->thread, 0);
}
