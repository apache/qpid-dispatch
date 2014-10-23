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

#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/ctools.h>
#include <stdio.h>
#include <pthread.h>

struct sys_mutex_t {
    pthread_mutex_t mutex;
#ifndef NDEBUG
    // In a debug build, used to assert correct use of mutex.
    int             acquired;
#endif
};

// NOTE: normally it is incorrect for an assert expression to have side effects,
// since it could change the behavior between a debug and a release build.  In
// this case however the mutex->acquired field only exists in a debug build, so
// we want operations on mutex->acquired to be compiled out of a release build.
#define ACQUIRE(mutex) assert(!mutex->acquired++)
#define RELEASE(mutex) assert(!--mutex->acquired)


sys_mutex_t *sys_mutex(void)
{
    sys_mutex_t *mutex = NEW(sys_mutex_t);
    pthread_mutex_init(&(mutex->mutex), 0);
#ifndef NDEBUG
    mutex->acquired = 0;
#endif
    return mutex;
}


void sys_mutex_free(sys_mutex_t *mutex)
{
    assert(!mutex->acquired);
    pthread_mutex_destroy(&(mutex->mutex));
    free(mutex);
}


void sys_mutex_lock(sys_mutex_t *mutex)
{
    pthread_mutex_lock(&(mutex->mutex));
    ACQUIRE(mutex);
}


void sys_mutex_unlock(sys_mutex_t *mutex)
{
    RELEASE(mutex);
    pthread_mutex_unlock(&(mutex->mutex));
}


struct sys_cond_t {
    pthread_cond_t cond;
};


sys_cond_t *sys_cond(void)
{
    sys_cond_t *cond = NEW(sys_cond_t);
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
    RELEASE(held_mutex);
    pthread_cond_wait(&(cond->cond), &(held_mutex->mutex));
    ACQUIRE(held_mutex);
}


void sys_cond_signal(sys_cond_t *cond)
{
    pthread_cond_signal(&(cond->cond));
}


void sys_cond_signal_all(sys_cond_t *cond)
{
    pthread_cond_broadcast(&(cond->cond));
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
    pthread_rwlock_wrlock(&(lock->lock));
}


void sys_rwlock_rdlock(sys_rwlock_t *lock)
{
    pthread_rwlock_rdlock(&(lock->lock));
}


void sys_rwlock_unlock(sys_rwlock_t *lock)
{
    pthread_rwlock_unlock(&(lock->lock));
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
