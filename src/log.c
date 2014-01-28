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

#include "log_private.h"
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/threading.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TEXT_MAX 512
#define LIST_MAX 1000

typedef struct qd_log_entry_t qd_log_entry_t;

struct qd_log_entry_t {
    DEQ_LINKS(qd_log_entry_t);
    const char     *module;
    int             cls;
    const char     *file;
    int             line;
    time_t          time;
    char            text[TEXT_MAX];
};

ALLOC_DECLARE(qd_log_entry_t);
ALLOC_DEFINE(qd_log_entry_t);

DEQ_DECLARE(qd_log_entry_t, qd_log_list_t);

struct qd_log_source_t {
    DEQ_LINKS(qd_log_source_t);
    const char *module_name;
    //int mask;
    //int print_timestamp;
    //int print_to_stderr;
    //int print_to_file;
    //int print_to_syslog;
    //FILE *file;
};

DEQ_DECLARE(qd_log_source_t, qd_log_source_list_t);


static qd_log_list_t         entries;
static sys_mutex_t          *log_lock = 0;
static qd_log_source_list_t  source_list;
static int                   mask = QD_LOG_INFO;

static const char *cls_prefix(int cls)
{
    switch (cls) {
    case QD_LOG_TRACE    : return "TRACE";
    case QD_LOG_DEBUG    : return "DEBUG";
    case QD_LOG_INFO     : return "INFO";
    case QD_LOG_NOTICE   : return "NOTICE";
    case QD_LOG_WARNING  : return "WARNING";
    case QD_LOG_ERROR    : return "ERROR";
    case QD_LOG_CRITICAL : return "CRITICAL";
    }

    return "";
}

qd_log_source_t *qd_log_source(const char *module)
{
    qd_log_source_t *source = NEW(qd_log_source_t);
    memset(source, 0, sizeof(qd_log_source_t));
    DEQ_ITEM_INIT(source);
    source->module_name = module;

    // TODO - Configure the source

    return source;
}

void qd_log_impl(qd_log_source_t *source, int cls, const char *file, int line, const char *fmt, ...)
{
    if (!(cls & mask))
        return;

    qd_log_entry_t *entry = new_qd_log_entry_t();
    DEQ_ITEM_INIT(entry);
    entry->module = source->module_name;
    entry->cls    = cls;
    entry->file   = file;
    entry->line   = line;
    time(&entry->time);

    char ctime[100];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(entry->text, TEXT_MAX, fmt, ap);
    va_end(ap);
    ctime_r(&entry->time, ctime);
    ctime[24] = '\0';
    fprintf(stderr, "%s %s (%s) %s\n", ctime, entry->module, cls_prefix(cls), entry->text);

    sys_mutex_lock(log_lock);
    DEQ_INSERT_TAIL(entries, entry);
    if (DEQ_SIZE(entries) > LIST_MAX) {
        entry = DEQ_HEAD(entries);
        DEQ_REMOVE_HEAD(entries);
        free_qd_log_entry_t(entry);
    }
    sys_mutex_unlock(log_lock);
}

void qd_log_set_mask(int _mask)
{
    mask = _mask;
}


void qd_log_initialize(void)
{
    DEQ_INIT(entries);
    DEQ_INIT(source_list);
    log_lock = sys_mutex();
}


void qd_log_finalize(void)
{
}


