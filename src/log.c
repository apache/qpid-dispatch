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
#include <syslog.h>

#define TEXT_MAX 512
#define LIST_MAX 1000
#define LOG_MAX 640

int qd_log_max_len() { return TEXT_MAX; }

typedef struct qd_log_entry_t qd_log_entry_t;

struct qd_log_entry_t {
    DEQ_LINKS(qd_log_entry_t);
    const char     *module;
    int             level;
    char           *file;
    int             line;
    time_t          time;
    char            text[TEXT_MAX];
};

ALLOC_DECLARE(qd_log_entry_t);
ALLOC_DEFINE(qd_log_entry_t);

DEQ_DECLARE(qd_log_entry_t, qd_log_list_t);

// Ref-counted log sink, may be shared by several sources.
typedef struct qd_log_sink_t {
    int refcount;
    char *name;
    bool syslog;
    FILE *file;
    DEQ_LINKS(struct qd_log_sink_t);
} qd_log_sink_t;

DEQ_DECLARE(qd_log_sink_t, qd_log_sink_list_t);

static qd_log_sink_list_t log_sinks;

static qd_log_sink_t* find_log_sink(const char* name) {
    qd_log_sink_t* sink = DEQ_HEAD(log_sinks);
    DEQ_FIND(sink, strcmp(sink->name, name) == 0);
    return sink;
}

qd_log_sink_t* qd_log_sink(const char* name) {
    qd_log_sink_t* sink = find_log_sink(name);
    if (sink) 
	sink->refcount++;
    else {
	sink = NEW(qd_log_sink_t);
	*sink = (qd_log_sink_t){ 1, strdup(name), };
	if (strcmp(name, "stderr") == 0) {
	    sink->file = stderr;
	}
	else if (strcmp(name, "syslog") == 0) {
	    openlog(0, 0, LOG_DAEMON);
	    sink->syslog = true;
	}
	else {
	    sink->file = fopen(name, "w");
	    if (!sink->file) {
		char msg[LOG_MAX];
		snprintf(msg, sizeof(msg), "Failed to open log file '%s'", name);
		perror(msg);
		exit(1);		/* TODO aconway 2014-05-22: better error handling */
	    }
	}
	DEQ_INSERT_TAIL(log_sinks, sink);
    }
    return sink;
}

void qd_log_sink_free(qd_log_sink_t* sink) {
    if (!sink) return;
    assert(sink->refcount);
    if (--sink->refcount == 0) {
	free(sink->name);
	if (sink->file && sink->file != stderr) fclose(sink->file);
	if (sink->syslog) closelog();
	free(sink);
    }
}

struct qd_log_source_t {
    DEQ_LINKS(qd_log_source_t);
    const char *module;
    int mask;
    int timestamp;
    bool syslog;
    qd_log_sink_t *sink;
};

DEQ_DECLARE(qd_log_source_t, qd_log_source_list_t);


static qd_log_list_t         entries;
static sys_mutex_t          *log_lock = 0;
static sys_mutex_t          *log_source_lock = 0;
static qd_log_source_list_t  source_list;
static qd_log_source_t      *default_log_source=0;
static qd_log_source_t      *logging_log_source=0;

static const int nlevels = 7;
static const int level_bits[] ={QD_LOG_TRACE, QD_LOG_DEBUG, QD_LOG_INFO, QD_LOG_NOTICE, QD_LOG_WARNING, QD_LOG_ERROR, QD_LOG_CRITICAL};
static const char* level_names[] = {"TRACE", "DEBUG", "INFO", "NOTICE", "WARNING", "ERROR", "CRITICAL"};
static const int level_syslog_priorities[] = {LOG_DEBUG, LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING, LOG_ERR, LOG_CRIT};
static const int all_bits = ((QD_LOG_CRITICAL-1) | QD_LOG_CRITICAL);

static int level_index(int level) {
    int i = 0;
    while (i < nlevels && level_bits[i] != level) ++i;
    return (i == nlevels) ? -1 : i;
}

static const char *level_str(int level) {
    int i = level_index(level);
    return i >= 0 ? level_names[i] : "NONE";
}

static int level_syslog(int level) {
    int i = level_index(level);
    return i >= 0 ? level_syslog_priorities[i] : LOG_NOTICE;
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
    DEQ_FIND(src, strcasecmp(module, src->module) == 0);
    return src;
}

static void write_log(int level, qd_log_source_t *log_source, const char *fmt, ...)
{
    qd_log_sink_t* sink = log_source->sink ? log_source->sink : default_log_source->sink;

    if (!sink) return;

    /* FIXME aconway 2014-05-22: move output logic to sink */
    char log_str[LOG_MAX];
    va_list arglist;
    va_start(arglist, fmt);
    vsnprintf(log_str, LOG_MAX, fmt, arglist);
    va_end(arglist);

    if (sink->file) fputs(log_str, sink->file);
    if (sink->syslog) syslog(level_syslog(level), "%s", log_str);
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
	log_source->sink = 0;
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
    qd_log_sink_free(src->sink);
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
    entry->level  = level;
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
    write_log(level, source, "%s %s (%s) %s\n", ctime, entry->module, level_str(level), entry->text);

    sys_mutex_lock(log_lock);
    DEQ_INSERT_TAIL(entries, entry);
    if (DEQ_SIZE(entries) > LIST_MAX) {
        entry = DEQ_HEAD(entries);
        DEQ_REMOVE_HEAD(entries);
        free(entry->file);
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
    default_log_source->sink = 0;
    logging_log_source = qd_log_source("LOGGING");
}


void qd_log_finalize(void) {
    for (qd_log_source_t *src = DEQ_HEAD(source_list); src != 0; src = DEQ_HEAD(source_list)) {
        DEQ_REMOVE_HEAD(source_list);
        qd_log_source_free(src);
    }

    for (qd_log_entry_t *entry = DEQ_HEAD(entries); entry; entry = DEQ_HEAD(entries)) {
        DEQ_REMOVE_HEAD(entries);
        free(entry->file);
        free_qd_log_entry_t(entry);
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

	const char* output = ITEM_STRING("output");
	if (output) src->sink = qd_log_sink(output);
	sys_mutex_unlock(log_source_lock);
    }
    qd_log(logging_log_source, QD_LOG_INFO, "Logging system configured");
}

