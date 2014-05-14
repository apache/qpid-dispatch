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
#include <qpid/dispatch/config.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/dispatch.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/threading.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TEXT_MAX 512
#define LIST_MAX 1000
#define LOG_MAX 640

typedef struct qd_log_entry_t qd_log_entry_t;

struct qd_log_entry_t {
    DEQ_LINKS(qd_log_entry_t);
    const char     *module;
    int             level;
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
    const char *module;
    int mask;
    int timestamp;
    int print;
    int stderr;
    char* file;
    int syslog;
    //FILE *file;
};

DEQ_DECLARE(qd_log_source_t, qd_log_source_list_t);


static qd_log_list_t         entries;
static sys_mutex_t          *log_lock = 0;
static sys_mutex_t          *log_source_lock = 0;
static qd_log_source_list_t  source_list;
static qd_log_source_t      *default_log_source=0;
static qd_log_source_t      *logging_log_source=0;

static const int nlevels = 7;
static const char* level_names[]={"TRACE", "DEBUG", "INFO", "NOTICE", "WARNING", "ERROR", "CRITICAL"};
static const int level_bits[] ={QD_LOG_TRACE, QD_LOG_DEBUG, QD_LOG_INFO, QD_LOG_NOTICE, QD_LOG_WARNING, QD_LOG_ERROR, QD_LOG_CRITICAL};
static const int all_bits = ((QD_LOG_CRITICAL-1) | QD_LOG_CRITICAL);

static const char *level_str(int level) {
    if (level == 0) return "NONE";
    int i = 0;
    while (i < nlevels && level_bits[i] != level) ++i;
    return (i == nlevels) ? "NONE" : level_names[i];
}


static int get_mask(const char *level) {
    if (strcasecmp(level, "NONE") == 0) return 0;
    int i = 0;
    while (i < nlevels && strcasecmp(level_names[i], level) != 0) ++i;
    if (i == nlevels) {
	qd_log(logging_log_source, QD_LOG_ERROR, "'%s' is not a valid log level. Should be one of {NONE, TRACE, DEBUG, INFO, NOTICE, WARNING, ERROR, CRITICAL}. Defaulting to INFO", level);
	return get_mask("INFO");
    }
    return  all_bits & ~(level_bits[i]-1);
}

/// Caller must hold log_source_lock
static qd_log_source_t* lookup_log_source_lh(const char *module)
{
    if (strcasecmp(module, "DEFAULT") == 0)
	return default_log_source;
    qd_log_source_t *src = DEQ_HEAD(source_list);
    while(src && strcasecmp(module, src->module) != 0)
	src = DEQ_NEXT(src);
    return src;
}

static void write_log(qd_log_source_t *log_source, const char *fmt, ...)
{
    int stderr_ = log_source->stderr == -1 ? default_log_source->stderr : log_source->stderr;
    int syslog = log_source->syslog == -1 ? default_log_source->syslog : log_source->syslog;
    const char* file = log_source->file == 0 ? default_log_source->file : log_source->file;

    if (!(stderr_ || syslog || file)) return;

    char log_str[LOG_MAX];
    va_list arglist;
    va_start(arglist, fmt);
    vsnprintf(log_str, LOG_MAX, fmt, arglist);
    va_end(arglist);

    if (stderr_) fputs(log_str, stderr);
    /* FIXME aconway 2014-05-12: unfinished
       if (syslog) ...
       if (file) ...
    */
}

/// Caller must hold the log_source_lock
static qd_log_source_t *qd_log_source_lh(const char *module)
{
    qd_log_source_t *log_source = lookup_log_source_lh(module);
    if (!log_source)
    {
	log_source = NEW(qd_log_source_t);
	memset(log_source, 0, sizeof(qd_log_source_t));
	DEQ_ITEM_INIT(log_source);
	log_source->module = module;
	log_source->mask = -1;
	log_source->timestamp = -1;
	log_source->stderr = -1;
	log_source->syslog = -1;
	log_source->file = 0;
        DEQ_INSERT_TAIL(source_list, log_source);
    }
    return log_source;
}

qd_log_source_t *qd_log_source(const char *module)
{
    sys_mutex_lock(log_source_lock);
    qd_log_source_t* src = qd_log_source_lh(module);
    sys_mutex_unlock(log_source_lock);
    return src;
}

void qd_log_source_free(qd_log_source_t* src) {
    free(src->file);
    free(src);
}

bool qd_log_enabled(qd_log_source_t *source, int level) {
    if (!source) return false;
    int mask = source->mask == -1 ? default_log_source->mask : source->mask;
    return level & mask;
}

void qd_log_impl(qd_log_source_t *source, int level, const char *file, int line, const char *fmt, ...)
{
    if (!qd_log_enabled(source, level)) return;

    qd_log_entry_t *entry = new_qd_log_entry_t();
    DEQ_ITEM_INIT(entry);
    entry->module = source->module;
    entry->level    = level;
    entry->file   = file ? strdup(file) : 0;
    entry->line   = line;
    time(&entry->time);

    char ctime[100];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(entry->text, TEXT_MAX, fmt, ap);
    va_end(ap);
    ctime_r(&entry->time, ctime);
    ctime[24] = '\0';
    write_log(source, "%s %s (%s) %s\n", ctime, entry->module, level_str(level), entry->text);

    sys_mutex_lock(log_lock);
    DEQ_INSERT_TAIL(entries, entry);
    if (DEQ_SIZE(entries) > LIST_MAX) {
        entry = DEQ_HEAD(entries);
        DEQ_REMOVE_HEAD(entries);
        free_qd_log_entry_t(entry);
    }
    sys_mutex_unlock(log_lock);
}

void qd_log_initialize(void)
{
    DEQ_INIT(entries);
    DEQ_INIT(source_list);
    log_lock = sys_mutex();
    log_source_lock = sys_mutex();

    default_log_source = NEW(qd_log_source_t);
    memset(default_log_source, 0, sizeof(qd_log_source_t));
    default_log_source->module = "DEFAULT";
    // Only report errors until we have configured the logging system.
    default_log_source->mask = get_mask("ERROR");
    default_log_source->timestamp = 1;
    default_log_source->stderr = 1;
    default_log_source->syslog = 0;
    default_log_source->file = 0;
    logging_log_source = qd_log_source("LOGGING");
}


void qd_log_finalize(void) {
    for (qd_log_source_t *src = DEQ_HEAD(source_list); src != 0; src = DEQ_HEAD(source_list)) {
	DEQ_REMOVE_HEAD(source_list);
	qd_log_source_free(src);
    }
}

///@return 0,1 or -1 if not present
static int get_optional_bool(const qd_dispatch_t *qd, const char* item, int i, const char* name) {
    if (!qd_config_item_exists(qd, item, i, name)) return -1;
    return qd_config_item_value_bool(qd, item, i, name);
}

#define ITEM_STRING(NAME) qd_config_item_value_string(qd, "log", i, NAME)
#define ITEM_OPT_BOOL(NAME) get_optional_bool(qd, "log", i, NAME)

void qd_log_configure(const qd_dispatch_t *qd)
{
    if (!qd) return;
    // Default to INFO now that we are configuring the logging system.
    default_log_source->mask = get_mask("INFO");
    int count = qd_config_item_count(qd, "log");
    for (int i=0; i < count; i++) {
	sys_mutex_lock(log_source_lock);
	const char* module = ITEM_STRING("module");
	qd_log_source_t *src = qd_log_source_lh(module);
	src->module = ITEM_STRING("module");
	src->mask = get_mask(ITEM_STRING("level"));
	src->timestamp = ITEM_OPT_BOOL("timestamp");
	src->stderr = ITEM_OPT_BOOL("stderr");
	src->syslog = ITEM_OPT_BOOL("syslog");
	const char* file = ITEM_STRING("file");
	src->file = file ? strdup(file) : 0;
	sys_mutex_unlock(log_source_lock);
    }
    qd_log(logging_log_source, QD_LOG_INFO, "Logging system configured");
}

