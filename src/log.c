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

#include "python_private.h"  // must be first!

#include "qpid/dispatch/log.h"

#include "aprintf.h"
#include "entity.h"
#include "entity_cache.h"
#include "log_private.h"
#include "server_private.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/atomic.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/dispatch.h"
#include "qpid/dispatch/threading.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#define TEXT_MAX QD_LOG_TEXT_MAX
#define LOG_MAX (QD_LOG_TEXT_MAX+128)
#define LIST_MAX 1000

const char *QD_LOG_STATS_TYPE = "logStats";

static qd_log_source_t      *default_log_source=0;

int qd_log_max_len() { return TEXT_MAX; }

typedef struct qd_log_entry_t qd_log_entry_t;

struct qd_log_entry_t {
    DEQ_LINKS(qd_log_entry_t);
    char           *module;
    int             level;
    char           *file;
    int             line;
    struct timeval  time;
    char            text[TEXT_MAX];
};

ALLOC_DECLARE(qd_log_entry_t);
ALLOC_DEFINE(qd_log_entry_t);

DEQ_DECLARE(qd_log_entry_t, qd_log_list_t);
static qd_log_list_t         entries = {0};

static void qd_log_entry_free_lh(qd_log_entry_t* entry) {
    DEQ_REMOVE(entries, entry);
    free(entry->file);
    free(entry->module);
    free_qd_log_entry_t(entry);
}

// Ref-counted log sink, may be shared by several sources.
typedef struct log_sink_t {
    sys_atomic_t ref_count;
    char *name;
    bool syslog;
    FILE *file;
    DEQ_LINKS(struct log_sink_t);
} log_sink_t;

DEQ_DECLARE(log_sink_t, log_sink_list_t);

static log_sink_list_t sink_list = {0};

const char *format = "%Y-%m-%d %H:%M:%S.%%06lu %z";
bool utc = false;

void qd_log_global_options(const char* _format, bool _utc) {
    if (_format) format = _format;
    utc = _utc;
}


void qd_log_formatted_time(struct timeval *time, char *buf, size_t buflen)
{
    time_t sec = time->tv_sec;
    struct tm *tm_time = utc ? gmtime(&sec) : localtime(&sec);
    char fmt[100];
    strftime(fmt, sizeof fmt, format, tm_time);
    snprintf(buf, buflen, fmt, time->tv_usec);
}


static const char* SINK_STDOUT = "stdout";
static const char* SINK_STDERR = "stderr";
static const char* SINK_SYSLOG = "syslog";
static const char* SOURCE_DEFAULT = "DEFAULT";

static log_sink_t* find_log_sink_lh(const char* name) {
    log_sink_t* sink = DEQ_HEAD(sink_list);
    DEQ_FIND(sink, strcmp(sink->name, name) == 0);
    return sink;
}

// Must hold the log_source_lock
static void log_sink_free_lh(log_sink_t* sink) {
    if (!sink) return;
    assert(sink->ref_count);

    if (sys_atomic_dec(&sink->ref_count) == 1) {
        DEQ_REMOVE(sink_list, sink);
        free(sink->name);
        if (sink->file && sink->file != stderr)
            fclose(sink->file);
        if (sink->syslog)
            closelog();
        free(sink);
    }
}

static log_sink_t* log_sink_lh(const char* name) {
    log_sink_t* sink = find_log_sink_lh(name);
    if (sink) {
        sys_atomic_inc(&sink->ref_count);
    }
    else {
        bool syslog = false;
        FILE *file = 0;

        if (strcmp(name, SINK_STDERR) == 0) {
            file = stderr;
        }
        else if (strcmp(name, SINK_STDOUT) == 0) {
            file = stdout;
        }
        else if (strcmp(name, SINK_SYSLOG) == 0) {
            openlog(0, 0, LOG_DAEMON);
            syslog = true;
        }
        else {
            file = fopen(name, "a");
        }



        //If file is not there, return 0.
        // We are not logging an error here since we are already holding the log_source_lock
        // Writing a log message will try to re-obtain the log_source_lock lock and cause a deadlock.
        if (!file && !syslog) {
            return 0;
        }

        sink = NEW(log_sink_t);
        ZERO(sink);
        sys_atomic_init(&sink->ref_count, 1);
        sink->name = strdup(name);
        sink->syslog = syslog;
        sink->file = file;
        DEQ_INSERT_TAIL(sink_list, sink);

    }
    return sink;
}


typedef enum {DEFAULT, NONE, TRACE, DEBUG, INFO, NOTICE, WARNING, ERROR, CRITICAL, N_LEVELS} level_index_t;
#define MIN_VALID_LEVEL_INDEX TRACE
#define MAX_VALID_LEVEL_INDEX CRITICAL
#define N_LEVEL_INDICES (MAX_VALID_LEVEL_INDEX - MIN_VALID_LEVEL_INDEX + 1)
#define LEVEL_INDEX(LEVEL) ((LEVEL) - TRACE)

struct qd_log_source_t {
    DEQ_LINKS(qd_log_source_t);
    char *module;
    int mask;
    int includeTimestamp;       /* boolean or -1 means not set */
    int includeSource;          /* boolean or -1 means not set */
    bool syslog;
    log_sink_t *sink;
    uint64_t severity_histogram[N_LEVEL_INDICES];
};

DEQ_DECLARE(qd_log_source_t, qd_log_source_list_t);

static sys_mutex_t          *log_source_lock = 0;
static qd_log_source_list_t  source_list = {0};


typedef struct level_t {
    const char* name;
    int bit;     // QD_LOG bit
    int mask;    // Bit or higher
    const int syslog;
} level_t;

#define ALL_BITS (QD_LOG_CRITICAL | (QD_LOG_CRITICAL-1))

#define LEVEL(name, QD_LOG, SYSLOG) { name, QD_LOG,  ALL_BITS & ~(QD_LOG-1), SYSLOG }

static level_t levels[] = {
    {"default", -1, -1, 0},
    {"none", 0, 0, 0},
    LEVEL("trace",    QD_LOG_TRACE, LOG_DEBUG), /* syslog has no trace level */
    LEVEL("debug",    QD_LOG_DEBUG, LOG_DEBUG),
    LEVEL("info",     QD_LOG_INFO, LOG_INFO),
    LEVEL("notice",   QD_LOG_NOTICE, LOG_NOTICE),
    LEVEL("warning",  QD_LOG_WARNING, LOG_WARNING),
    LEVEL("error",    QD_LOG_ERROR, LOG_ERR),
    LEVEL("critical", QD_LOG_CRITICAL, LOG_CRIT)
};

static const level_t invalid_level = {"invalid", -2, -2, 0};

static char level_names[TEXT_MAX] = {0}; /* Set up in qd_log_initialize */

/// Return NULL and set qd_error if not a valid bit.
static const level_t* level_for_bit(int bit) {
    level_index_t i = 0;
    while (i < N_LEVELS && levels[i].bit != bit) ++i;
    if (i == N_LEVELS) {
        return &invalid_level;
    }
    return &levels[i];
}

/// Return NULL and set qd_error if not a valid level.
static const level_t* level_for_name(const char *name, int len) {
    level_index_t i = 0;
    while (i < N_LEVELS && strncasecmp(levels[i].name, name, len) != 0) ++i;
    if (i == N_LEVELS) {
        return &invalid_level;
    }
    return &levels[i];
}

/*
  Return -1 and set qd_error if not a valid bit.
  Translate so that the min valid level index is 0.
*/
static int level_index_for_bit(int bit) {
    level_index_t i = MIN_VALID_LEVEL_INDEX;
    while ( i <= MAX_VALID_LEVEL_INDEX ) {
        if ( levels[i].bit == bit )
            return (int) (i - MIN_VALID_LEVEL_INDEX);
        ++ i;
    }

    qd_error(QD_ERROR_CONFIG, "'%d' is not a valid log level bit.", bit);
    return -1;
}

/// Return the name of log level or 0 if not found.
static const char* level_name(int level) {
    return (0 <= level && level < N_LEVELS) ? levels[level].name : NULL;
}

static const char *SEPARATORS=", ;:";

/// Calculate the bit mask for a log enable string. Return -1 and set qd_error on error.
static int enable_mask(const char *enable_) {
    char *enable = qd_malloc(strlen(enable_) + 1);
    strcpy(enable, enable_);
    char *saveptr = 0;
    int mask = 0;
    for (const char *token = strtok_r(enable, SEPARATORS, &saveptr);
         token;
         token = strtok_r(NULL, SEPARATORS, &saveptr))
    {
        int len = strlen(token);
        int plus = (len > 0 && token[len-1] == '+') ? 1 : 0;
        const level_t* level = level_for_name(token, len-plus);
        mask |= (plus ? level->mask : level->bit);
    }
    free(enable);
    return mask;
}

/// Caller must hold log_source_lock
static qd_log_source_t* lookup_log_source_lh(const char *module)
{
    if (strcasecmp(module, SOURCE_DEFAULT) == 0)
        return default_log_source;
    qd_log_source_t *src = DEQ_HEAD(source_list);
    DEQ_FIND(src, strcasecmp(module, src->module) == 0);
    return src;
}

static bool default_bool(int value, int default_value) {
    return value == -1 ? default_value : value;
}

static void write_log(qd_log_source_t *log_source, qd_log_entry_t *entry)
{
    log_sink_t* sink = log_source->sink ? log_source->sink : default_log_source->sink;
    if (!sink) return;

    char log_str[LOG_MAX];
    char *begin = log_str;
    char *end = log_str + LOG_MAX;

    const level_t *level = level_for_bit(entry->level);
    if (level->bit == -2) {
        level = &levels[INFO];
        qd_error_clear();
    }

    if (default_bool(log_source->includeTimestamp, default_log_source->includeTimestamp)) {
        char buf[100];
        buf[0] = '\0';

        qd_log_formatted_time(&entry->time, buf, 100);

        aprintf(&begin, end, "%s ", buf);
    }

    aprintf(&begin, end, "%s (%s) %s", entry->module, level->name, entry->text);

    if (default_bool(log_source->includeSource, default_log_source->includeSource) && entry->file)
        aprintf(&begin, end, " (%s:%d)", entry->file, entry->line);

    aprintf(&begin, end, "\n");

    if (sink->file) {
        if (fputs(log_str, sink->file) == EOF) {
            char msg[TEXT_MAX];
            snprintf(msg, sizeof(msg), "Cannot write log output to '%s'", sink->name);
            perror(msg);
            exit(1);
        };
        fflush(sink->file);
    }
    if (sink->syslog) {
        int syslog_level = level->syslog;
        if (syslog_level != -1)
            syslog(syslog_level, "%s", log_str);
    }
}

/// Reset the log source to the default state
static void qd_log_source_defaults(qd_log_source_t *log_source) {
    log_source->mask = -1;
    log_source->includeTimestamp = -1;
    log_source->includeSource = -1;
    log_source->sink = 0;
    memset ( log_source->severity_histogram, 0, sizeof(uint64_t) * (N_LEVEL_INDICES) );
}

/// Caller must hold the log_source_lock
static qd_log_source_t *qd_log_source_lh(const char *module)
{
    qd_log_source_t *log_source = lookup_log_source_lh(module);
    if (!log_source)
    {
        log_source = NEW(qd_log_source_t);
        ZERO(log_source);
        log_source->module = (char*) malloc(strlen(module) + 1);
        strcpy(log_source->module, module);
        qd_log_source_defaults(log_source);
        DEQ_INSERT_TAIL(source_list, log_source);
        qd_entity_cache_add(QD_LOG_STATS_TYPE, log_source);
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

qd_log_source_t *qd_log_source_reset(const char *module)
{
    sys_mutex_lock(log_source_lock);
    qd_log_source_t* src = qd_log_source_lh(module);
    qd_log_source_defaults(src);
    sys_mutex_unlock(log_source_lock);
    return src;
}

static void qd_log_source_free_lh(qd_log_source_t* src) {
    DEQ_REMOVE(source_list, src);
    log_sink_free_lh(src->sink);
    free(src->module);
    free(src);
}

bool qd_log_enabled(qd_log_source_t *source, qd_log_level_t level) {
    if (!source) return false;
    int mask = source->mask == -1 ? default_log_source->mask : source->mask;
    return level & mask;
}

void qd_vlog_impl(qd_log_source_t *source, qd_log_level_t level, bool check_level, const char *file, int line, const char *fmt, va_list ap)
{
    /*-----------------------------------------------
      Count this log-event in this log's histogram
      whether or not this log is currently enabled.
      We can always decide not to look at it later,
      based on its used/unused status.
    -----------------------------------------------*/
    int level_index = level_index_for_bit(level);
    if (level_index < 0)
        qd_error_clear();
    else
        source->severity_histogram[level_index]++;

    if (check_level) {
        if (!qd_log_enabled(source, level))
            return;
    }

    // Bounded buffer of log entries, keep most recent.
    qd_log_entry_t *entry = new_qd_log_entry_t();
    DEQ_ITEM_INIT(entry);

    //
    // Obtain the log_source_lock global lock. We need to do this, if not, the qd_log_entity() function
    // could free the log_source->sink from underneath you and replace it with a new sink.
    // Once we obtain this lock, we only release the lock once the log line is written to the sink.
    //
    sys_mutex_lock(log_source_lock);
    entry->module = source->module ? strdup(source->module) : 0;
    entry->level  = level;
    entry->file   = file ? strdup(file) : 0;
    entry->line   = line;
    gettimeofday(&entry->time, NULL);
    vsnprintf(entry->text, TEXT_MAX, fmt, ap);
    write_log(source, entry);
    DEQ_INSERT_TAIL(entries, entry);
    if (DEQ_SIZE(entries) > LIST_MAX)
        qd_log_entry_free_lh(DEQ_HEAD(entries));
    sys_mutex_unlock(log_source_lock);
}

void qd_log_impl_v1(qd_log_source_t *source, qd_log_level_t level,  const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    qd_vlog_impl(source, level, false, file, line, fmt, ap);
    va_end(ap);
}

void qd_log_impl(qd_log_source_t *source, qd_log_level_t level, const char *file, int line, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  qd_vlog_impl(source, level, true, file, line, fmt, ap);
  va_end(ap);
}

static PyObject *inc_none() { Py_INCREF(Py_None); return Py_None; }

/// Return the log buffer up to limit as a python list. Called by management agent.
QD_EXPORT PyObject *qd_log_recent_py(long limit) {
    if (PyErr_Occurred()) return NULL;
    PyObject *list = PyList_New(0);
    PyObject *py_entry = NULL;
    if (!list) goto error;
    qd_log_entry_t *entry = DEQ_TAIL(entries);
    while (entry && limit) {
        const int ENTRY_SIZE=6;
        py_entry = PyList_New(ENTRY_SIZE);
        if (!py_entry) goto error;
        int i = 0;
        // NOTE: PyList_SetItem steals a reference so no leak here.
        PyList_SetItem(py_entry, i++, PyUnicode_FromString(entry->module));
        const char* level = level_name( level_index_for_bit(entry->level) + 2 );
        PyList_SetItem(py_entry, i++, level ? PyUnicode_FromString(level) : inc_none());
        PyList_SetItem(py_entry, i++, PyUnicode_FromString(entry->text));
        PyList_SetItem(py_entry, i++, entry->file ? PyUnicode_FromString(entry->file) : inc_none());
        PyList_SetItem(py_entry, i++, entry->file ? PyLong_FromLong(entry->line) : inc_none());
        PyList_SetItem(py_entry, i++, PyLong_FromLongLong((PY_LONG_LONG)entry->time.tv_sec));
        assert(i == ENTRY_SIZE);
        if (PyErr_Occurred()) goto error;
        PyList_Insert(list, 0, py_entry);
        Py_DECREF(py_entry);
        if (limit > 0) --limit;
        entry = DEQ_PREV(entry);
    }
    return list;
 error:
    Py_XDECREF(list);
    Py_XDECREF(py_entry);
    return NULL;
}

void qd_log_initialize(void)
{
    DEQ_INIT(entries);
    DEQ_INIT(source_list);
    DEQ_INIT(sink_list);

    // Set up level_names for use in error messages.
    char *begin = level_names, *end = level_names+sizeof(level_names);
    aprintf(&begin, end, "%s", levels[NONE].name);
    for (level_index_t i = NONE + 1; i < N_LEVELS; ++i)
        aprintf(&begin, end, ", %s", levels[i].name);

    log_source_lock = sys_mutex();

    default_log_source = qd_log_source(SOURCE_DEFAULT);
    default_log_source->mask = levels[INFO].mask;
    default_log_source->includeTimestamp = true;
    default_log_source->includeSource = 0;
    default_log_source->sink = log_sink_lh(SINK_STDERR);
}


void qd_log_finalize(void) {
    while (DEQ_HEAD(source_list))
        qd_log_source_free_lh(DEQ_HEAD(source_list));
    while (DEQ_HEAD(entries))
        qd_log_entry_free_lh(DEQ_HEAD(entries));
    while (DEQ_HEAD(sink_list))
        log_sink_free_lh(DEQ_HEAD(sink_list));
    default_log_source = NULL;  // stale value would misconfigure new router started again in the same process
}

QD_EXPORT qd_error_t qd_log_entity(qd_entity_t *entity)
{
    qd_error_clear();

    char* module = 0;
    char *outputFile = 0;
    char *enable = 0;
    int include_timestamp = 0;
    int include_source = 0;

    bool has_enable = false;
    bool has_output_file = false;
    bool has_include_timestamp = false;
    bool has_include_source = false;
    bool is_sink_syslog = false;
    bool trace_enabled = false;
    bool error_in_output = false;
    bool error_in_enable = false;

    do {
        //
        // A module attribute MUST be specified for a log entity.
        // Every other attribute is optional.
        //
        module = qd_entity_get_string(entity, "module");

        //
        // If the module is not specified, there is nothing to do, just log an
        // error and break out of this do loop.
        //
        QD_ERROR_BREAK();

        //
        // Obtain all attributes from the entity before obtaining the log_source_lock.
        // We do this because functions like qd_entity_get_string and qd_entity_get_bool ultimately call qd_vlog_impl() which
        // also holds the log_source_lock when calling write_log().
        //

        if (qd_entity_has(entity, "outputFile")) {
            has_output_file = true;
            outputFile = qd_entity_get_string(entity, "outputFile");
            QD_ERROR_BREAK();
        }

        if (qd_entity_has(entity, "enable")) {
            has_enable = true;
            enable = qd_entity_get_string(entity, "enable");
            QD_ERROR_BREAK();
        }

        if (qd_entity_has(entity, "includeTimestamp")) {
            has_include_timestamp = true;
            include_timestamp = qd_entity_get_bool(entity, "includeTimestamp");
            QD_ERROR_BREAK();
        }

        if (qd_entity_has(entity, "includeSource")) {
            has_include_source = true;
            include_source = qd_entity_get_bool(entity, "includeSource");
            QD_ERROR_BREAK();
        }

        //
        // Obtain the log_source_lock lock. This lock is also used when write_log() is called.
        //
        sys_mutex_lock(log_source_lock);

        qd_log_source_t *src = qd_log_source_lh(module); /* The original(already existing) log source */

        if (has_output_file) {
            log_sink_t* sink = log_sink_lh(outputFile);
            if (!sink) {
                error_in_output = true;
                sys_mutex_unlock(log_source_lock);
                break;
            }

            // DEFAULT source may already have a sink, so free the old sink first
            if (src->sink) {
                log_sink_free_lh(src->sink);
            }

            // Assign the new sink
            src->sink = sink;

            if (src->sink->syslog) {
                // Timestamp should be off for syslog.
                is_sink_syslog = true;
                src->includeTimestamp = 0;
            }
        }

        if (has_enable) {
            int mask = enable_mask(enable);

            if (mask < -1) {
                error_in_enable = true;
                sys_mutex_unlock(log_source_lock);
                break;
            }
            else {
                src->mask = mask;
            }

            if (qd_log_enabled(src, QD_LOG_TRACE)) {
                trace_enabled = true;
            }
        }

        if (has_include_timestamp && !is_sink_syslog) {
            // Timestamp should be off for syslog.
            src->includeTimestamp = include_timestamp;
        }

        if (has_include_source) {
            src->includeSource = include_source;
        }

        sys_mutex_unlock(log_source_lock);

    } while(0);

    if (error_in_output) {
        char msg[TEXT_MAX];
        snprintf(msg, sizeof(msg), "Failed to open log file '%s'", outputFile);
        qd_error(QD_ERROR_CONFIG, msg);
    }
    if (error_in_enable) {
        qd_error(QD_ERROR_CONFIG, "'%s' is not a valid log level. Should be one of {%s}.",  enable, level_names);
    }

    if (module)
        free(module);
    if (outputFile)
        free(outputFile);
    if (enable)
        free(enable);

    //
    // If trace logging is enabled, loop thru all connections in the router and call the pn_transport_set_tracer callback
    // so proton frame trace can be output as part of the router trace log.
    //
    if (trace_enabled) {
        qd_server_trace_all_connections();
    }

    return qd_error_code();
}

void qd_format_string(char* buf, int buf_size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, buf_size, fmt, args);
    va_end(args);
}

QD_EXPORT qd_error_t qd_entity_refresh_logStats(qd_entity_t* entity, void *impl)
{
    qd_log_source_t *log = (qd_log_source_t*)impl;
    char identity_str[TEXT_MAX];
    snprintf(identity_str, TEXT_MAX - 1,"logStats/%s", log->module);

    qd_entity_set_long(entity,   "traceCount",    log->severity_histogram[LEVEL_INDEX(TRACE)]);
    qd_entity_set_long(entity,   "debugCount",    log->severity_histogram[LEVEL_INDEX(DEBUG)]);
    qd_entity_set_long(entity,   "infoCount",     log->severity_histogram[LEVEL_INDEX(INFO)]);
    qd_entity_set_long(entity,   "noticeCount",   log->severity_histogram[LEVEL_INDEX(NOTICE)]);
    qd_entity_set_long(entity,   "warningCount",  log->severity_histogram[LEVEL_INDEX(WARNING)]);
    qd_entity_set_long(entity,   "errorCount",    log->severity_histogram[LEVEL_INDEX(ERROR)]);
    qd_entity_set_long(entity,   "criticalCount", log->severity_histogram[LEVEL_INDEX(CRITICAL)]);
    qd_entity_set_string(entity, "name",          log->module);
    qd_entity_set_string(entity, "identity",      identity_str);

    return QD_ERROR_NONE;
}
