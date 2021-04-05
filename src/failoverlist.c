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

#include "qpid/dispatch/failoverlist.h"

#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/error.h"

#include <ctype.h>
#include <string.h>

struct qd_failover_list_t {
    qd_failover_item_list_t  item_list;
    char                    *text;
};


static void qd_fol_remove_whitespace(char *text) {
    char *from = text;
    char *to   = text;

    while (*from) {
        if (isgraph(*from))
            *to++ = *from;
        from++;
    }
    *to = '\0';
}


static char *qd_fol_next(char *text, const char *separator)
{
    char *next = strstr(text, separator);
    if (next) {
        *next = '\0';
        next += strlen(separator);
    }
    return next;
}


/* Sets qd_error if there is an error */
static qd_failover_item_t *qd_fol_item(char *text)
{
    qd_error_clear();
    char *after_scheme = qd_fol_next(text, "://");
    char *scheme       = after_scheme ? text : 0;
    char *host         = after_scheme ? after_scheme : text;
    char *port         = qd_fol_next(host, ":");

    if (strlen(host) == 0) {
        qd_error(QD_ERROR_VALUE, "No network host in failover item");
        return 0;
    }

    qd_failover_item_t *item = NEW(qd_failover_item_t);
    ZERO(item);
    item->scheme   = scheme;
    item->host     = host;
    item->port     = port ? port : "5672";
    item->hostname = 0;
    return item;
}


qd_failover_list_t *qd_failover_list(const char *text)
{
    qd_failover_list_t *list = NEW(qd_failover_list_t);
    ZERO(list);

    qd_error_clear();
    list->text = (char*) malloc(strlen(text) + 1);
    strcpy(list->text, text);

    qd_fol_remove_whitespace(list->text);
    char *cursor = list->text;
    char *next;
    do {
        next = qd_fol_next(cursor, ",");
        qd_failover_item_t *item = qd_fol_item(cursor);
        if (item == 0) {
            qd_failover_list_free(list);
            return 0;
        }
        DEQ_INSERT_TAIL(list->item_list, item);
        cursor = next;
    } while (cursor && *cursor);

    return list;
}


void qd_failover_list_free(qd_failover_list_t *list)
{
    qd_failover_item_t *item = DEQ_HEAD(list->item_list);
    while (item) {
        DEQ_REMOVE_HEAD(list->item_list);
        free(item);
        item = DEQ_HEAD(list->item_list);
    }
    free(list->text);
    free(list);
}


int qd_failover_list_size(const qd_failover_list_t *list)
{
    return (int) DEQ_SIZE(list->item_list);
}


const char *qd_failover_list_scheme(const qd_failover_list_t *list, int index)
{
    qd_failover_item_t *item = DEQ_HEAD(list->item_list);
    while (index > 0 && item) {
        index--;
        item = DEQ_NEXT(item);
    }
    return item ? item->scheme : 0;
}


const char *qd_failover_list_host(const qd_failover_list_t *list, int index)
{
    qd_failover_item_t *item = DEQ_HEAD(list->item_list);
    while (index > 0 && item) {
        index--;
        item = DEQ_NEXT(item);
    }
    return item ? item->host : 0;
}


const char *qd_failover_list_port(const qd_failover_list_t *list, int index)
{
    qd_failover_item_t *item = DEQ_HEAD(list->item_list);
    while (index > 0 && item) {
        index--;
        item = DEQ_NEXT(item);
    }
    return item ? item->port : 0;
}


const char *qd_failover_list_hostname(const qd_failover_list_t *list, int index)
{
    qd_failover_item_t *item = DEQ_HEAD(list->item_list);
    while (index > 0 && item) {
        index--;
        item = DEQ_NEXT(item);
    }
    return item ? item->hostname : 0;
}

