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

#include "adaptor_utils.h"

#include <proton/netaddr.h>
#include <string.h>


#define RAW_BUFFER_BATCH  16


char *qda_raw_conn_get_address(pn_raw_connection_t *socket)
{
    const pn_netaddr_t *netaddr = pn_raw_connection_remote_addr(socket);
    char buffer[1024];
    int len = pn_netaddr_str(netaddr, buffer, 1024);
    if (len <= 1024) {
        return strdup(buffer);
    } else {
        return strndup(buffer, 1024);
    }
}


void qda_raw_conn_get_read_buffers(pn_raw_connection_t *conn, qd_buffer_list_t *blist, uintmax_t *length)
{
    pn_raw_buffer_t buffs[RAW_BUFFER_BATCH];

    DEQ_INIT(*blist);
    uintmax_t len = 0;
    size_t n;
    while ((n = pn_raw_connection_take_read_buffers(conn, buffs, RAW_BUFFER_BATCH)) != 0) {
        for (size_t i = 0; i < n; ++i) {
            qd_buffer_t *qd_buf = (qd_buffer_t*)buffs[i].context;
            assert(qd_buf);
            if (buffs[i].size) {
                // set content length:
                qd_buffer_insert(qd_buf, buffs[i].size);
                len += buffs[i].size;
                DEQ_INSERT_TAIL(*blist, qd_buf);
            } else {  // ignore empty buffers
                qd_buffer_free(qd_buf);
            }
        }
    }

    *length = len;
}


int qda_raw_conn_grant_read_buffers(pn_raw_connection_t *conn)
{
    pn_raw_buffer_t buffs[RAW_BUFFER_BATCH];
    if (pn_raw_connection_is_read_closed(conn))
        return 0;

    int granted = 0;
    size_t count = pn_raw_connection_read_buffers_capacity(conn);
    while (count) {
        size_t batch_ct = 0;
        for (int i = 0; i < RAW_BUFFER_BATCH; ++i) {
            qd_buffer_t *buf = qd_buffer();
            buffs[i].context  = (intptr_t)buf;
            buffs[i].bytes    = (char*) qd_buffer_base(buf);
            buffs[i].capacity = qd_buffer_capacity(buf);
            buffs[i].size     = 0;
            buffs[i].offset   = 0;
            batch_ct += 1;
            count -= 1;
            if (count == 0)
                break;
        }
        pn_raw_connection_give_read_buffers(conn, buffs, batch_ct);
        granted += batch_ct;
    }

    return granted;
}


int qda_raw_conn_write_buffers(pn_raw_connection_t *conn, qd_buffer_list_t *blist, size_t offset)
{
    pn_raw_buffer_t buffs[RAW_BUFFER_BATCH];
    size_t count = pn_raw_connection_write_buffers_capacity(conn);
    count = MIN(count, DEQ_SIZE(*blist));
    int sent = 0;

    // Since count is set to ensure that we never run out of capacity or
    // buffers to send we can avoid checking that on every loop

    while (count) {
        size_t batch_ct = 0;

        for (int i = 0; i < RAW_BUFFER_BATCH; ++i) {
            qd_buffer_t *buf = DEQ_HEAD(*blist);
            DEQ_REMOVE_HEAD(*blist);

            buffs[i].context  = (intptr_t)buf;
            buffs[i].bytes    = (char*)qd_buffer_base(buf);
            buffs[i].capacity = 0;
            buffs[i].size     = qd_buffer_size(buf) - offset;
            buffs[i].offset   = offset;
            offset = 0; // all succeeding bufs start at base

            batch_ct += 1;
            count -= 1;
            if (count == 0)
                break;
        }
        pn_raw_connection_write_buffers(conn, buffs, batch_ct);
        sent += batch_ct;
    }

    return sent;
}


void qda_raw_conn_free_write_buffers(pn_raw_connection_t *conn)
{
    pn_raw_buffer_t buffs[RAW_BUFFER_BATCH];
    size_t n;
    while ((n = pn_raw_connection_take_written_buffers(conn, buffs, RAW_BUFFER_BATCH)) != 0) {
        for (size_t i = 0; i < n; ++i) {
            qd_buffer_t *qd_buf = (qd_buffer_t*)buffs[i].context;
            assert(qd_buf);
            qd_buffer_free(qd_buf);
        }
    }
}

