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

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include "test_case.h"
#include <qpid/dispatch.h>

#define THREAD_COUNT 4
#define OCTET_COUNT  100

static qd_dispatch_t *qd;
static sys_mutex_t   *test_lock;

static int   call_count;
static char  stored_error[512];

static int   write_count;
static int   read_count;
static int   fd[2];
static qd_user_fd_t *ufd_write;
static qd_user_fd_t *ufd_read;


static void ufd_handler(void *context, qd_user_fd_t *ufd)
{
    long    dir = (long) context;
    char    buffer;
    ssize_t len;
    static  int in_read  = 0;
    static  int in_write = 0;

    if (dir == 0) { // READ
        in_read++;
        assert(in_read == 1);
        if (!qd_user_fd_is_readable(ufd_read)) {
            sprintf(stored_error, "Expected Readable");
            qd_server_stop(qd);
        } else {
            len = read(fd[0], &buffer, 1);
            if (len == 1) {
                read_count++;
                if (read_count == OCTET_COUNT)
                    qd_server_stop(qd);
            }
            qd_user_fd_activate_read(ufd_read);
        }
        in_read--;
    } else {        // WRITE
        in_write++;
        assert(in_write == 1);
        if (!qd_user_fd_is_writeable(ufd_write)) {
            sprintf(stored_error, "Expected Writable");
            qd_server_stop(qd);
        } else {
            if (write(fd[1], "X", 1) < 0) abort();

            write_count++;
            if (write_count < OCTET_COUNT)
                qd_user_fd_activate_write(ufd_write);
        }
        in_write--;
    }
}


static void fd_test_start(void *context, int unused)
{
    if (++call_count == THREAD_COUNT) {
        qd_user_fd_activate_read(ufd_read);
    }
}


static char* test_user_fd(void *context)
{
    int res;

    call_count = 0;
    qd_server_set_start_handler(qd, fd_test_start, 0);
    qd_server_set_user_fd_handler(qd, ufd_handler);

    stored_error[0] = 0x0;

    res = pipe(fd); // Don't use pipe2 because it's not available on RHEL5
    if (res != 0) return "Error creating pipe2";

    for (int i = 0; i < 2; i++) {
        int flags = fcntl(fd[i], F_GETFL);
        flags |= O_NONBLOCK;
        if (fcntl(fd[i], F_SETFL, flags) < 0) {
            perror("fcntl");
            return "Failed to set socket to non-blocking";
        }
    }

    ufd_write = qd_user_fd(qd, fd[1], (void*) 1);
    ufd_read  = qd_user_fd(qd, fd[0], (void*) 0);

    qd_server_run(qd);
    close(fd[0]);
    close(fd[1]);

    if (stored_error[0])            return stored_error;
    if (write_count - OCTET_COUNT > 2) sprintf(stored_error, "Excessively high Write Count: %d", write_count);
    if (read_count != OCTET_COUNT)  sprintf(stored_error, "Incorrect Read Count: %d", read_count);;

    if (stored_error[0]) return stored_error;
    return 0;
}


int server_tests(qd_dispatch_t *_qd)
{
    int result = 0;
    test_lock = sys_mutex();

    qd = _qd;

    TEST_CASE(test_user_fd, 0);

    sys_mutex_free(test_lock);
    return result;
}

