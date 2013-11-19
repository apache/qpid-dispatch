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

#include <qpid/dispatch/iovec.h>
#include <qpid/dispatch/alloc.h>
#include <string.h>

#define QD_IOVEC_MAX 64

struct qd_iovec_t {
    struct iovec  iov_array[QD_IOVEC_MAX];
    struct iovec *iov;
    int           iov_count;
};


ALLOC_DECLARE(qd_iovec_t);
ALLOC_DEFINE(qd_iovec_t);


qd_iovec_t *qd_iovec(int vector_count)
{
    qd_iovec_t *iov = new_qd_iovec_t();
    if (!iov)
        return 0;

    memset(iov, 0, sizeof(qd_iovec_t));

    iov->iov_count = vector_count;
    if (vector_count > QD_IOVEC_MAX)
        iov->iov = (struct iovec*) malloc(sizeof(struct iovec) * vector_count);
    else
        iov->iov = &iov->iov_array[0];

    return iov;
}


void qd_iovec_free(qd_iovec_t *iov)
{
    if (!iov)
        return;

    if (iov->iov && iov->iov != &iov->iov_array[0])
        free(iov->iov);

    free_qd_iovec_t(iov);
}


struct iovec *qd_iovec_array(qd_iovec_t *iov)
{
    if (!iov)
        return 0;
    return iov->iov;
}


int qd_iovec_count(qd_iovec_t *iov)
{
    if (!iov)
        return 0;
    return iov->iov_count;
}

