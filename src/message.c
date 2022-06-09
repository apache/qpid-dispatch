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

#include "qpid/dispatch/message.h"

#include "aprintf.h"
#include "compose_private.h"
#include "connection_manager_private.h"
#include "message_private.h"
#include "policy.h"
#include "buffer_field_api.h"

#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/error.h"
#include "qpid/dispatch/iterator.h"
#include "qpid/dispatch/log.h"
#include "qpid/dispatch/threading.h"

#include <proton/object.h>

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>


#define LOCK   sys_mutex_lock
#define UNLOCK sys_mutex_unlock

const char *STR_AMQP_NULL = "null";
const char *STR_AMQP_TRUE = "T";
const char *STR_AMQP_FALSE = "F";

static const unsigned char * const MSG_HDR_LONG                 = (unsigned char*) "\x00\x80\x00\x00\x00\x00\x00\x00\x00\x70";
static const unsigned char * const MSG_HDR_SHORT                = (unsigned char*) "\x00\x53\x70";
static const unsigned char * const DELIVERY_ANNOTATION_LONG     = (unsigned char*) "\x00\x80\x00\x00\x00\x00\x00\x00\x00\x71";
static const unsigned char * const DELIVERY_ANNOTATION_SHORT    = (unsigned char*) "\x00\x53\x71";
static const unsigned char * const MESSAGE_ANNOTATION_LONG      = (unsigned char*) "\x00\x80\x00\x00\x00\x00\x00\x00\x00\x72";
static const unsigned char * const MESSAGE_ANNOTATION_SHORT     = (unsigned char*) "\x00\x53\x72";
static const unsigned char * const PROPERTIES_LONG              = (unsigned char*) "\x00\x80\x00\x00\x00\x00\x00\x00\x00\x73";
static const unsigned char * const PROPERTIES_SHORT             = (unsigned char*) "\x00\x53\x73";
static const unsigned char * const APPLICATION_PROPERTIES_LONG  = (unsigned char*) "\x00\x80\x00\x00\x00\x00\x00\x00\x00\x74";
static const unsigned char * const APPLICATION_PROPERTIES_SHORT = (unsigned char*) "\x00\x53\x74";
static const unsigned char * const BODY_DATA_LONG               = (unsigned char*) "\x00\x80\x00\x00\x00\x00\x00\x00\x00\x75";
static const unsigned char * const BODY_DATA_SHORT              = (unsigned char*) "\x00\x53\x75";
static const unsigned char * const BODY_SEQUENCE_LONG           = (unsigned char*) "\x00\x80\x00\x00\x00\x00\x00\x00\x00\x76";
static const unsigned char * const BODY_SEQUENCE_SHORT          = (unsigned char*) "\x00\x53\x76";
static const unsigned char * const BODY_VALUE_LONG              = (unsigned char*) "\x00\x80\x00\x00\x00\x00\x00\x00\x00\x77";
static const unsigned char * const BODY_VALUE_SHORT             = (unsigned char*) "\x00\x53\x77";
static const unsigned char * const FOOTER_LONG                  = (unsigned char*) "\x00\x80\x00\x00\x00\x00\x00\x00\x00\x78";
static const unsigned char * const FOOTER_SHORT                 = (unsigned char*) "\x00\x53\x78";
static const unsigned char * const TAGS_LIST                    = (unsigned char*) "\x45\xc0\xd0";
static const unsigned char * const TAGS_MAP                     = (unsigned char*) "\xc1\xd1";
static const unsigned char * const TAGS_BINARY                  = (unsigned char*) "\xa0\xb0";
static const unsigned char * const TAGS_ANY                     = (unsigned char*) "\x45\xc0\xd0\xc1\xd1\xa0\xb0"
    "\xa1\xb1\xa3\xb3\xe0\xf0"
    "\x40\x56\x41\x42\x50\x60\x70\x52\x43\x80\x53\x44\x51\x61\x71\x54\x81\x55\x72\x82\x74\x84\x94\x73\x83\x98";


static const char * const section_names[QD_DEPTH_ALL + 1] = {
    [QD_DEPTH_NONE]                   = "none",
    [QD_DEPTH_HEADER]                 = "header",
    [QD_DEPTH_DELIVERY_ANNOTATIONS]   = "delivery annotations",
    [QD_DEPTH_MESSAGE_ANNOTATIONS]    = "message annotations",
    [QD_DEPTH_PROPERTIES]             = "properties",
    [QD_DEPTH_APPLICATION_PROPERTIES] = "application properties",
    [QD_DEPTH_BODY]                   = "body",
    [QD_DEPTH_ALL]                    = "footer"
};

PN_HANDLE(PN_DELIVERY_CTX)

ALLOC_DEFINE_CONFIG(qd_message_t, sizeof(qd_message_pvt_t), 0, 0);
ALLOC_DEFINE(qd_message_content_t);
ALLOC_DEFINE(qd_message_stream_data_t);

typedef void (*buffer_process_t) (void *context, const unsigned char *base, int length);

qd_log_source_t* log_source = 0;

qd_log_source_t* qd_message_log_source()
{
    if(log_source)
        return log_source;
    else {
        qd_message_initialize();
        return log_source;
    }
}

void qd_message_initialize() {
    log_source = qd_log_source("MESSAGE");
}

int qd_message_repr_len() { return qd_log_max_len(); }

/**
 * Quote non-printable characters suitable for log messages. Output in buffer.
 */
static void quote(char* bytes, int n, char **begin, char *end) {
    for (char* p = bytes; p < bytes+n; ++p) {
        if (isprint(*p) || isspace(*p))
            aprintf(begin, end, "%c", (int)*p);
        else
            aprintf(begin, end, "\\%02hhx", *p);
    }
}

/**
 * Populates the buffer with formatted epoch_time
 */
static void format_time(pn_timestamp_t epoch_time, char *format, char *buffer, size_t len)
{
    struct timeval local_timeval;
    local_timeval.tv_sec = epoch_time/1000;
    local_timeval.tv_usec = (epoch_time%1000) * 1000;

    time_t local_time_t;
    local_time_t = local_timeval.tv_sec;

    struct tm *local_tm;
    char fmt[100];
    local_tm = localtime(&local_time_t);

    if (local_tm != NULL) {
        strftime(fmt, sizeof fmt, format, local_tm);
        snprintf(buffer, len, fmt, local_timeval.tv_usec / 1000);
    }
}

/**
 * Print the bytes of a parsed_field as characters, with pre/post quotes.
 */
static void print_parsed_field_string(qd_parsed_field_t *parsed_field,
                                      const char *pre, const char *post,
                                      char **begin, char *end) {
    qd_iterator_t *i = qd_parse_raw(parsed_field);
    if (i) {
        aprintf(begin, end, "%s", pre);
        while (end - *begin > 1 &&  !qd_iterator_end(i)) {
            char c = qd_iterator_octet(i);
            quote(&c, 1, begin, end);
        }
        aprintf(begin, end, "%s", post);
    }
}

/**
 * Tries to print the string representation of the parsed field content based on
 * the tag of the parsed field.  Some tag types have not been dealt with. Add
 * code as and when required.
 */
static void print_parsed_field(qd_parsed_field_t *parsed_field, char **begin, char *end)
{
   uint8_t   tag    = qd_parse_tag(parsed_field);
   switch (tag) {
       case QD_AMQP_NULL:
           aprintf(begin, end, "%s", STR_AMQP_NULL);
           break;

       case QD_AMQP_BOOLEAN:
       case QD_AMQP_TRUE:
       case QD_AMQP_FALSE:
           aprintf(begin, end, "%s", qd_parse_as_uint(parsed_field) ? STR_AMQP_TRUE: STR_AMQP_FALSE);
           break;

       case QD_AMQP_BYTE:
       case QD_AMQP_SHORT:
       case QD_AMQP_INT:
       case QD_AMQP_SMALLINT: {
         char str[11];
         int32_t int32_val = qd_parse_as_int(parsed_field);
         snprintf(str, 10, "%"PRId32"", int32_val);
         aprintf(begin, end, "%s", str);
         break;
       }

       case QD_AMQP_UBYTE:
       case QD_AMQP_USHORT:
       case QD_AMQP_UINT:
       case QD_AMQP_SMALLUINT:
       case QD_AMQP_UINT0: {
           char str[11];
           uint32_t uint32_val = qd_parse_as_uint(parsed_field);
           snprintf(str, 11, "%"PRIu32"", uint32_val);
           aprintf(begin, end, "%s", str);
           break;
       }
       case QD_AMQP_ULONG:
       case QD_AMQP_SMALLULONG:
       case QD_AMQP_ULONG0: {
           char str[21];
           uint64_t uint64_val = qd_parse_as_ulong(parsed_field);
           snprintf(str, 20, "%"PRIu64"", uint64_val);
           aprintf(begin, end, "%s", str);
           break;
       }
       case QD_AMQP_TIMESTAMP: {
           char creation_time[100]; //string representation of creation time.
           pn_timestamp_t creation_timestamp = qd_parse_as_ulong(parsed_field);
           if (creation_timestamp > 0) {
               format_time(creation_timestamp, "%Y-%m-%d %H:%M:%S.%%03lu %z", creation_time, 100);
               aprintf(begin, end, "\"%s\"", creation_time);
           }
           break;
       }
       case QD_AMQP_LONG:
       case QD_AMQP_SMALLLONG: {
           char str[21];
           int64_t int64_val = qd_parse_as_long(parsed_field);
           snprintf(str, 20, "%"PRId64"", int64_val);
           aprintf(begin, end, "%s", str);
           break;
       }
       case QD_AMQP_FLOAT:
       case QD_AMQP_DOUBLE:
       case QD_AMQP_DECIMAL32:
       case QD_AMQP_DECIMAL64:
       case QD_AMQP_DECIMAL128:
       case QD_AMQP_UTF32:
       case QD_AMQP_UUID:
           break; //TODO

       case QD_AMQP_VBIN8:
       case QD_AMQP_VBIN32:
         print_parsed_field_string(parsed_field, "b\"", "\"", begin, end);
         break;

       case QD_AMQP_STR8_UTF8:
       case QD_AMQP_STR32_UTF8:
         print_parsed_field_string(parsed_field, "\"", "\"", begin, end);
         break;

       case QD_AMQP_SYM8:
       case QD_AMQP_SYM32:
         print_parsed_field_string(parsed_field, ":\"", "\"", begin, end);
         break;

       case QD_AMQP_MAP8:
       case QD_AMQP_MAP32: {
           uint32_t count = qd_parse_sub_count(parsed_field);
           if (count > 0) {
               aprintf(begin, end, "%s", "{");
           }
           for (uint32_t idx = 0; idx < count; idx++) {
               qd_parsed_field_t *sub_key  = qd_parse_sub_key(parsed_field, idx);
               // The keys of this map are restricted to be of type string
               // (which excludes the possibility of a null key)
               print_parsed_field(sub_key, begin, end);

               aprintf(begin, end, "%s", "=");

               qd_parsed_field_t *sub_value = qd_parse_sub_value(parsed_field, idx);

               print_parsed_field(sub_value, begin, end);

               if ((idx + 1) < count)
                   aprintf(begin, end, "%s", ", ");
           }
           if (count > 0) {
               aprintf(begin, end, "%s", "}");
           }
           break;
       }
       case QD_AMQP_LIST0:
       case QD_AMQP_LIST8:
       case QD_AMQP_LIST32: {
           uint32_t count = qd_parse_sub_count(parsed_field);
           if (count > 0) {
               aprintf(begin, end, "%s", "[");
           }
           for (uint32_t idx = 0; idx < count; idx++) {
               qd_parsed_field_t *sub_value = qd_parse_sub_value(parsed_field, idx);
               print_parsed_field(sub_value, begin, end);
               if ((idx + 1) < count)
                  aprintf(begin, end, "%s", ", ");
           }

           if (count > 0) {
               aprintf(begin, end, "%s", "]");
           }

           break;
       }
       default:
           break;
   }
}

/* Print field if enabled by log bits, leading comma if !*first */
static void print_field(
    qd_message_t *msg, int field, const char *name,
    qd_log_bits flags, bool *first, char **begin, char *end)
{
    if (is_log_component_enabled(flags, name)) {
        qd_iterator_t* iter = (field == QD_FIELD_APPLICATION_PROPERTIES) ?
            qd_message_field_iterator(msg, field) :
            qd_message_field_iterator_typed(msg, field);
        if (iter) {
            qd_parsed_field_t *parsed_field = qd_parse(iter);
            if (qd_parse_ok(parsed_field)) {
                if (*first) {
                    *first = false;
                    aprintf(begin, end, "%s=", name);
                } else {
                    aprintf(begin, end, ", %s=", name);
                }
                print_parsed_field(parsed_field, begin, end);
            }
            qd_parse_free(parsed_field);
            qd_iterator_free(iter);
        }
    }
}

static const char REPR_END[] = "}\0";

char* qd_message_repr(qd_message_t *msg, char* buffer, size_t len, qd_log_bits flags) {
    if (flags == 0
        || qd_message_check_depth(msg, QD_DEPTH_APPLICATION_PROPERTIES) != QD_MESSAGE_DEPTH_OK
        || !((qd_message_pvt_t *)msg)->content->section_application_properties.parsed) {
        return NULL;
    }
    char *begin = buffer;
    char *end = buffer + len - sizeof(REPR_END); /* Save space for ending */
    bool first = true;
    aprintf(&begin, end, "Message{", msg);
    print_field(msg, QD_FIELD_MESSAGE_ID, "message-id", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_USER_ID, "user-id", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_TO, "to", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_SUBJECT, "subject", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_REPLY_TO, "reply-to", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_CORRELATION_ID, "correlation-id", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_CONTENT_TYPE, "content-type", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_CONTENT_ENCODING, "content-encoding", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_ABSOLUTE_EXPIRY_TIME, "absolute-expiry-time", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_CREATION_TIME, "creation-time", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_GROUP_ID, "group-id", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_GROUP_SEQUENCE, "group-sequence", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_REPLY_TO_GROUP_ID, "reply-to-group-id", flags, &first, &begin, end);
    print_field(msg, QD_FIELD_APPLICATION_PROPERTIES, "app-properties", flags, &first, &begin, end);

    aprintf(&begin, end, "%s", REPR_END);   /* We saved space at the beginning. */
    return buffer;
}


/**
 * Return true if there is at least one consumable octet in the buffer chain
 * starting at *cursor.  If the cursor is beyond the end of the buffer, and there
 * is another buffer in the chain, move the cursor and buffer pointers to reference
 * the first octet in the next buffer.  Note that this movement does NOT constitute
 * advancement of the cursor in the buffer chain.
 */
static bool can_advance(unsigned char **cursor, qd_buffer_t **buffer)
{
    if (qd_buffer_cursor(*buffer) > *cursor)
        return true;

    if (DEQ_NEXT(*buffer)) {
        *buffer = DEQ_NEXT(*buffer);
        *cursor = qd_buffer_base(*buffer);
    }

    return qd_buffer_cursor(*buffer) > *cursor;
}


/**
 * Advance cursor through buffer chain by 'consume' bytes.
 * Cursor and buffer args are advanced to point to new position in buffer chain.
 *  - if the number of bytes in the buffer chain is less than or equal to
 *    the consume number then return false
 *  - the original buffer chain is not changed or freed.
 *
 * @param cursor Pointer into current buffer content
 * @param buffer pointer to current buffer
 * @param consume number of bytes to advance
 * @return true if all bytes consumed, false if not enough bytes available
 */
static bool advance(unsigned char **cursor, qd_buffer_t **buffer, int consume)
{
    if (!can_advance(cursor, buffer))
        return false;

    unsigned char *local_cursor = *cursor;
    qd_buffer_t   *local_buffer = *buffer;

    int remaining = qd_buffer_cursor(local_buffer) - local_cursor;
    while (consume > 0) {
        if (consume <= remaining) {
            local_cursor += consume;
            consume = 0;
        } else {
            if (!local_buffer->next)
                return false;

            consume -= remaining;
            local_buffer = local_buffer->next;
            local_cursor = qd_buffer_base(local_buffer);
            remaining = qd_buffer_size(local_buffer);
        }
    }

    *cursor = local_cursor;
    *buffer = local_buffer;

    return true;
}


/**
 * Advance cursor through buffer chain by 'consume' bytes.
 * Cursor and buffer args are advanced to point to new position in buffer chain.
 * Buffer content that is consumed is optionally passed to handler.
 *  - if the number of bytes in the buffer chain is less than or equal to
 *    the consume number then return the last buffer in the chain
 *    and a cursor pointing to the first unused byte in the buffer.
 *  - if the number of bytes in the buffer chain is greater than the consume
 *    number the returned buffer/cursor will point to the next available
 *    octet of data.
 *  - the original buffer chain is not changed or freed.
 *
 * @param cursor pointer into current buffer content
 * @param buffer pointer to current buffer
 * @param consume number of bytes to advance
 * @param handler pointer to processor function
 * @param context opaque argument for handler
 */
static void advance_guarded(const uint8_t **cursor, qd_buffer_t **buffer, int consume, buffer_process_t handler, void *context)
{
    const uint8_t *local_cursor = *cursor;
    qd_buffer_t   *local_buffer = *buffer;

    int remaining = qd_buffer_cursor(local_buffer) - local_cursor;
    while (consume > 0) {
        if (consume < remaining) {
            if (handler)
                handler(context, local_cursor, consume);
            local_cursor += consume;
            consume = 0;
        } else {
            if (handler)
                handler(context, local_cursor, remaining);
            consume -= remaining;
            if (!DEQ_NEXT(local_buffer)) {
                local_cursor = qd_buffer_cursor(local_buffer);
                break;
            }
            local_buffer = DEQ_NEXT(local_buffer);
            local_cursor = qd_buffer_base(local_buffer);
            remaining = qd_buffer_size(local_buffer);
        }
    }

    *cursor = local_cursor;
    *buffer = local_buffer;
}


/**
 * If there is an octet to be consumed, put it in octet and return true, else return false.
 */
static bool next_octet(unsigned char **cursor, qd_buffer_t **buffer, unsigned char *octet)
{
    if (can_advance(cursor, buffer)) {
        *octet = **cursor;
        advance(cursor, buffer, 1);
        return true;
    }
    return false;
}


static bool traverse_field(unsigned char **cursor, qd_buffer_t **buffer, qd_field_location_t *field)
{
    qd_buffer_t   *start_buffer = *buffer;
    unsigned char *start_cursor = *cursor;
    unsigned char  tag;
    unsigned char  octet;

    if (!next_octet(cursor, buffer, &tag))
        return false;

    int    consume    = 0;
    size_t hdr_length = 1;

    switch (tag & 0xF0) {
    case 0x40 :
        consume = 0;
        break;
    case 0x50 :
        consume = 1;
        break;
    case 0x60 :
        consume = 2;
        break;
    case 0x70 :
        consume = 4;
        break;
    case 0x80 :
        consume = 8;
        break;
    case 0x90 :
        consume = 16;
        break;

    case 0xB0 :
    case 0xD0 :
    case 0xF0 :
        hdr_length += 3;
        if (!next_octet(cursor, buffer, &octet))
            return false;
        consume |= ((int) octet) << 24;

        if (!next_octet(cursor, buffer, &octet))
            return false;
        consume |= ((int) octet) << 16;

        if (!next_octet(cursor, buffer, &octet))
            return false;
        consume |= ((int) octet) << 8;

        // Fall through to the next case...

    case 0xA0 :
    case 0xC0 :
    case 0xE0 :
        hdr_length++;
        if (!next_octet(cursor, buffer, &octet))
            return false;
        consume |= (int) octet;
        break;
    }

    if (!advance(cursor, buffer, consume))
        return false;

    if (field && !field->parsed) {
        field->buffer     = start_buffer;
        field->offset     = start_cursor - qd_buffer_base(start_buffer);
        field->length     = consume;
        field->hdr_length = hdr_length;
        field->parsed     = true;
        field->tag        = tag;
    }

    return true;
}


static int get_list_count(unsigned char **cursor, qd_buffer_t **buffer)
{
    unsigned char tag;
    unsigned char octet;

    if (!next_octet(cursor, buffer, &tag))
        return 0;

    int count = 0;

    switch (tag) {
    case 0x45 :     // list0
        break;
    case 0xd0 :     // list32
        //
        // Advance past the list length
        //
        if (!advance(cursor, buffer, 4))
            return 0;

        if (!next_octet(cursor, buffer, &octet))
            return 0;
        count |= ((int) octet) << 24;

        if (!next_octet(cursor, buffer, &octet))
            return 0;
        count |= ((int) octet) << 16;

        if (!next_octet(cursor, buffer, &octet))
            return 0;
        count |= ((int) octet) << 8;

        if (!next_octet(cursor, buffer, &octet))
            return 0;
        count |=  (int) octet;

        break;

    case 0xc0 :     // list8
        //
        // Advance past the list length
        //
        if (!advance(cursor, buffer, 1))
            return 0;

        if (!next_octet(cursor, buffer, &octet))
            return 0;
        count |= (int) octet;
        break;
    }

    return count;
}


// Validate a message section (header, body, etc).  This determines whether or
// not a given section is present and complete at the start of the buffer chain.
//
// The section is identified by a 'pattern' (a descriptor identifier, such as
// "MESSAGE_ANNOTATION_LONG" above).  The descriptor also provides a type
// 'tag', which MUST match else the section is invalid.
//
// Non-Body message sections are optional.  So if the pattern does NOT match
// then the section that the pattern represents is not present.  Whether or not
// this is acceptable is left to the caller.
//
// If the pattern and tag match, extract the length and verify that the entire
// section is present in the buffer chain.  If this is the case then store the
// start of the section in 'location' and advance '*buffer' and '*cursor' to
// the next section.
//
// if there is not enough of the section present in the buffer chain we need to
// wait until more data arrives and try again.
//
//
typedef enum {
    QD_SECTION_INVALID,   // invalid section (tag mismatch, duplicate section, etc).
    QD_SECTION_MATCH,
    QD_SECTION_NO_MATCH,
    QD_SECTION_NEED_MORE  // not enough data in the buffer chain - try again
} qd_section_status_t;

static qd_section_status_t message_section_check_LH(qd_message_content_t *content,
                                                    qd_buffer_t         **buffer,
                                                    unsigned char       **cursor,
                                                    const unsigned char  *pattern,
                                                    int                   pattern_length,
                                                    const unsigned char  *expected_tags,
                                                    qd_field_location_t  *location,
                                                    bool                  dup_ok,
                                                    bool                  protect_buffer)
{
    if (!*cursor || !can_advance(cursor, buffer))
        return QD_SECTION_NEED_MORE;

    qd_buffer_t   *test_buffer   = *buffer;
    unsigned char *test_cursor   = *cursor;
    unsigned char *end_of_buffer = qd_buffer_cursor(test_buffer);
    int            idx           = 0;

    while (idx < pattern_length && *test_cursor == pattern[idx]) {
        idx++;
        test_cursor++;
        if (test_cursor == end_of_buffer) {
            test_buffer = test_buffer->next;
            if (test_buffer == 0)
                return QD_SECTION_NEED_MORE;
            test_cursor = qd_buffer_base(test_buffer);
            end_of_buffer = test_cursor + qd_buffer_size(test_buffer);
        }
    }

    if (idx < pattern_length)
        return QD_SECTION_NO_MATCH;

    //
    // Pattern matched, check the tag
    //
    while (*expected_tags && *test_cursor != *expected_tags)
        expected_tags++;
    if (*expected_tags == 0)
        return QD_SECTION_INVALID;  // Error: Unexpected tag

    if (location->parsed && !dup_ok)
        return QD_SECTION_INVALID;  // Error: Duplicate section

    //
    // Pattern matched and tag is expected.  Mark the beginning of the section.
    //
    location->buffer     = *buffer;
    location->offset     = *cursor - qd_buffer_base(*buffer);
    location->length     = 0;
    location->hdr_length = pattern_length;

    //
    // Check that the full section is present, if so advance the pointers to
    // consume the whole section.
    //
    int pre_consume  = 1;  // Count the already extracted tag
    uint32_t consume = 0;
    unsigned char tag;
    unsigned char octet;

    if (!next_octet(&test_cursor, &test_buffer, &tag))
        return QD_SECTION_NEED_MORE;

    unsigned char tag_subcat = tag & 0xF0;

    // if there is no more data the only valid data type is a null type (0x40),
    // size is implied as 0
    if (!can_advance(&test_cursor, &test_buffer) && tag_subcat != 0x40)
        return QD_SECTION_NEED_MORE;

    switch (tag_subcat) {
        // fixed sizes:
    case 0x40: /* null */    break;
    case 0x50: consume = 1;  break;
    case 0x60: consume = 2;  break;
    case 0x70: consume = 4;  break;
    case 0x80: consume = 8;  break;
    case 0x90: consume = 16; break;

    case 0xB0:
    case 0xD0:
    case 0xF0:
        // uint32_t size field:
        pre_consume += 3;
        if (!next_octet(&test_cursor, &test_buffer, &octet))
            return QD_SECTION_NEED_MORE;
        consume |= ((uint32_t) octet) << 24;

        if (!next_octet(&test_cursor, &test_buffer, &octet))
            return QD_SECTION_NEED_MORE;
        consume |= ((uint32_t) octet) << 16;

        if (!next_octet(&test_cursor, &test_buffer, &octet))
            return QD_SECTION_NEED_MORE;
        consume |= ((uint32_t) octet) << 8;

        // Fall through to the next case...

    case 0xA0:
    case 0xC0:
    case 0xE0:
        // uint8_t size field
        pre_consume += 1;
        if (!next_octet(&test_cursor, &test_buffer, &octet))
            return QD_SECTION_NEED_MORE;
        consume |= (uint32_t) octet;
        break;
    }

    location->length = pre_consume + consume;
    if (consume) {
        if (!advance(&test_cursor, &test_buffer, consume)) {
            return QD_SECTION_NEED_MORE;  // whole section not fully received
        }
    }

    if (protect_buffer) {
        //
        // increment the reference count of the parsed section as location now
        // references it. Note that the cursor may have advanced to the octet after
        // the parsed section, so be careful not to include an extra buffer past
        // the end.  And cursor + buffer will be null if the parsed section ends at
        // the end of the buffer chain, so be careful of that, too!
        //
        bool buffers_protected = false;
        qd_buffer_t *start = *buffer;
        qd_buffer_t *last = test_buffer;
        if (last && last != start) {
            if (test_cursor == qd_buffer_base(last)) {
                // last does not include octets for the current section
                last = DEQ_PREV(last);
            }
        }

        while (start) {
            qd_buffer_inc_fanout(start);
            buffers_protected = true;
            if (start == last)
                break;
            start = DEQ_NEXT(start);
        }

        // DISPATCH-2191: protected buffers are never released - even after
        // being sent - because they are referenced by the content->section_xxx
        // location fields and remain valid for the life of the content
        // instance.  Since these buffers are never freed they must not be
        // included in the Q2 threshold check!
        if (buffers_protected) {
            content->protected_buffers = 0;
            start = DEQ_HEAD(content->buffers);
            while (start) {
                ++content->protected_buffers;
                if (start == last)
                    break;
                start = DEQ_NEXT(start);
            }
        }
    }

    location->parsed = 1;

    *cursor = test_cursor;
    *buffer = test_buffer;
    return QD_SECTION_MATCH;
}


// translate a field into its proper section of the message
static qd_message_field_t qd_field_section(qd_message_field_t field)
{
    switch (field) {

    case QD_FIELD_HEADER:
    case QD_FIELD_DELIVERY_ANNOTATION:
    case QD_FIELD_MESSAGE_ANNOTATION:
    case QD_FIELD_PROPERTIES:
    case QD_FIELD_APPLICATION_PROPERTIES:
    case QD_FIELD_BODY:
    case QD_FIELD_FOOTER:
        return field;

    case QD_FIELD_DURABLE:
    case QD_FIELD_PRIORITY:
    case QD_FIELD_TTL:
    case QD_FIELD_FIRST_ACQUIRER:
    case QD_FIELD_DELIVERY_COUNT:
        return QD_FIELD_HEADER;

    case QD_FIELD_MESSAGE_ID:
    case QD_FIELD_USER_ID:
    case QD_FIELD_TO:
    case QD_FIELD_SUBJECT:
    case QD_FIELD_REPLY_TO:
    case QD_FIELD_CORRELATION_ID:
    case QD_FIELD_CONTENT_TYPE:
    case QD_FIELD_CONTENT_ENCODING:
    case QD_FIELD_ABSOLUTE_EXPIRY_TIME:
    case QD_FIELD_CREATION_TIME:
    case QD_FIELD_GROUP_ID:
    case QD_FIELD_GROUP_SEQUENCE:
    case QD_FIELD_REPLY_TO_GROUP_ID:
        return QD_FIELD_PROPERTIES;

    default:
        assert(false);  // TBD: add new fields here
        return QD_FIELD_NONE;
    }
}


// get the field location of a field in the message properties (if it exists,
// else 0).
static qd_field_location_t *qd_message_properties_field(qd_message_t *msg, qd_message_field_t field)
{
    static const intptr_t offsets[] = {
        // position of the field's qd_field_location_t in the message content
        // object
        (intptr_t) &((qd_message_content_t*) 0)->field_message_id,
        (intptr_t) &((qd_message_content_t*) 0)->field_user_id,
        (intptr_t) &((qd_message_content_t*) 0)->field_to,
        (intptr_t) &((qd_message_content_t*) 0)->field_subject,
        (intptr_t) &((qd_message_content_t*) 0)->field_reply_to,
        (intptr_t) &((qd_message_content_t*) 0)->field_correlation_id,
        (intptr_t) &((qd_message_content_t*) 0)->field_content_type,
        (intptr_t) &((qd_message_content_t*) 0)->field_content_encoding,
        (intptr_t) &((qd_message_content_t*) 0)->field_absolute_expiry_time,
        (intptr_t) &((qd_message_content_t*) 0)->field_creation_time,
        (intptr_t) &((qd_message_content_t*) 0)->field_group_id,
        (intptr_t) &((qd_message_content_t*) 0)->field_group_sequence,
        (intptr_t) &((qd_message_content_t*) 0)->field_reply_to_group_id
    };
    // update table above if new fields need to be accessed:
    assert(QD_FIELD_MESSAGE_ID <= field && field <= QD_FIELD_REPLY_TO_GROUP_ID);

    qd_message_content_t *content = MSG_CONTENT(msg);
    if (!content->section_message_properties.parsed) {
        if (qd_message_check_depth(msg, QD_DEPTH_PROPERTIES) != QD_MESSAGE_DEPTH_OK || !content->section_message_properties.parsed)
            return 0;
    }

    const int index = field - QD_FIELD_MESSAGE_ID;
    qd_field_location_t *const location = (qd_field_location_t*) ((char*) content + offsets[index]);
    if (location->parsed)
        return location;

    // requested field not parsed out.  Need to parse out up to the requested field:
    qd_buffer_t   *buffer = content->section_message_properties.buffer;
    unsigned char *cursor = qd_buffer_base(buffer) + content->section_message_properties.offset;
    if (!advance(&cursor, &buffer, content->section_message_properties.hdr_length))
        return 0;
    if (index >= get_list_count(&cursor, &buffer))
        return 0;  // properties list too short

    int position = 0;
    while (position < index) {
        qd_field_location_t *f = (qd_field_location_t*) ((char*) content + offsets[position]);
        if (f->parsed) {
            if (!advance(&cursor, &buffer, f->hdr_length + f->length))
                return 0;
        } else // parse it out
            if (!traverse_field(&cursor, &buffer, f))
                return 0;
        position++;
    }

    // all fields previous to the target have now been parsed and cursor/buffer
    // are in the correct position, parse out the field:
    if (traverse_field(&cursor, &buffer, location))
        return location;

    return 0;
}


static void qd_message_parse_priority(qd_message_t *in_msg)
{
    qd_message_content_t *content  = MSG_CONTENT(in_msg);
    qd_iterator_t        *iter     = qd_message_field_iterator(in_msg, QD_FIELD_HEADER);

    SET_ATOMIC_FLAG(&content->priority_parsed);

    if (!!iter) {
        qd_parsed_field_t *field = qd_parse(iter);
        if (qd_parse_ok(field)) {
            if (qd_parse_is_list(field) && qd_parse_sub_count(field) >= 2) {
                qd_parsed_field_t *priority_field = qd_parse_sub_value(field, 1);
                if (qd_parse_tag(priority_field) != QD_AMQP_NULL) {
                    uint32_t value = qd_parse_as_uint(priority_field);
                    value = MIN(value, QDR_MAX_PRIORITY);
                    sys_atomic_set(&content->priority, value);
                }
            }
        }
        qd_parse_free(field);
        qd_iterator_free(iter);
    }
}


// Get the field's location in the buffer.  Return 0 if the field does not exist.
// Note that even if the field location is returned, it may contain a
// QD_AMQP_NULL value (qd_field_location->tag == QD_AMQP_NULL).
//
static qd_field_location_t *qd_message_field_location(qd_message_t *msg, qd_message_field_t field)
{
    qd_message_content_t *content = MSG_CONTENT(msg);
    qd_message_field_t section = qd_field_section(field);

    switch (section) {
    case QD_FIELD_HEADER:
        if (content->section_message_header.parsed ||
            (qd_message_check_depth(msg, QD_DEPTH_HEADER) == QD_MESSAGE_DEPTH_OK && content->section_message_header.parsed))
            return &content->section_message_header;
        break;

    case QD_FIELD_PROPERTIES:
        return qd_message_properties_field(msg, field);

    case QD_FIELD_DELIVERY_ANNOTATION:
        if (content->section_delivery_annotation.parsed ||
            (qd_message_check_depth(msg, QD_DEPTH_DELIVERY_ANNOTATIONS) == QD_MESSAGE_DEPTH_OK && content->section_delivery_annotation.parsed))
            return &content->section_delivery_annotation;
        break;

    case QD_FIELD_MESSAGE_ANNOTATION:
        if (content->section_message_annotation.parsed ||
            (qd_message_check_depth(msg, QD_DEPTH_MESSAGE_ANNOTATIONS) == QD_MESSAGE_DEPTH_OK && content->section_message_annotation.parsed))
            return &content->section_message_annotation;
        break;

    case QD_FIELD_APPLICATION_PROPERTIES:
        if (content->section_application_properties.parsed ||
            (qd_message_check_depth(msg, QD_DEPTH_APPLICATION_PROPERTIES) == QD_MESSAGE_DEPTH_OK && content->section_application_properties.parsed))
            return &content->section_application_properties;
        break;

    case QD_FIELD_BODY:
        if (content->section_body.parsed ||
            (qd_message_check_depth(msg, QD_DEPTH_BODY) == QD_MESSAGE_DEPTH_OK && content->section_body.parsed))
            return &content->section_body;
        break;

    case QD_FIELD_FOOTER:
        if (content->section_footer.parsed ||
            (qd_message_check_depth(msg, QD_DEPTH_ALL) == QD_MESSAGE_DEPTH_OK && content->section_footer.parsed))
            return &content->section_footer;
        break;

    default:
        assert(false); // TBD: add support as needed
        return 0;
    }

    return 0;
}


qd_message_t *qd_message()
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) new_qd_message_t();
    if (!msg)
        return 0;

    ZERO (msg);

    msg->content = new_qd_message_content_t();

    if (msg->content == 0) {
        free_qd_message_t((qd_message_t*) msg);
        return 0;
    }

    ZERO(msg->content);
    msg->content->lock = sys_mutex();
    sys_atomic_init(&msg->content->aborted, 0);
    sys_atomic_init(&msg->content->discard, 0);
    sys_atomic_init(&msg->content->no_body, 0);
    sys_atomic_init(&msg->content->oversize, 0);
    sys_atomic_init(&msg->content->priority, QDR_DEFAULT_PRIORITY);
    sys_atomic_init(&msg->content->priority_parsed, 0);
    sys_atomic_init(&msg->content->receive_complete, 0);
    sys_atomic_init(&msg->content->ref_count, 1);
    msg->content->parse_depth = QD_DEPTH_NONE;
    return (qd_message_t*) msg;
}


void qd_message_free(qd_message_t *in_msg)
{
    if (!in_msg) return;
    uint32_t rc;
    qd_message_pvt_t          *msg        = (qd_message_pvt_t*) in_msg;
    qd_message_q2_unblocker_t  q2_unblock = {0};

    free(msg->ma_to_override);

    sys_atomic_destroy(&msg->send_complete);

    qd_message_content_t *content = msg->content;

    if (msg->is_fanout) {
        //
        // Adjust the content's fanout count and decrement all buffer fanout
        // counts starting with the msg cursor.  If the buffer count drops to
        // zero we can free it.
        //
        LOCK(content->lock);

        // DISPATCH-2099: ensure all outstanding stream_data items associated
        // with this message have been returned since the underlying buffers
        // may be released
        assert(DEQ_IS_EMPTY(msg->stream_data_list));

        const bool was_blocked = !_Q2_holdoff_should_unblock_LH(content);
        qd_buffer_t *buf = msg->cursor.buffer;
        while (buf) {
            qd_buffer_t *next_buf = DEQ_NEXT(buf);
            if (qd_buffer_dec_fanout(buf) == 1) {
                DEQ_REMOVE(content->buffers, buf);
                qd_buffer_free(buf);
            }
            buf = next_buf;
        }
        --content->fanout;

        //
        // it is possible that we've freed enough buffers to clear Q2 holdoff
        //
        if (content->q2_input_holdoff
            && was_blocked
            && _Q2_holdoff_should_unblock_LH(content)) {
            content->q2_input_holdoff = false;
            q2_unblock = content->q2_unblocker;
        }

        UNLOCK(content->lock);
    }

    // the Q2 handler must be invoked outside the lock
    if (q2_unblock.handler)
        q2_unblock.handler(q2_unblock.context);

    rc = sys_atomic_dec(&content->ref_count) - 1;
    if (rc == 0) {
        if (content->ma_field_iter_in)
            qd_iterator_free(content->ma_field_iter_in);
        if (content->ma_pf_ingress)
            qd_parse_free(content->ma_pf_ingress);
        if (content->ma_pf_to_override)
            qd_parse_free(content->ma_pf_to_override);
        if (content->ma_pf_trace)
            qd_parse_free(content->ma_pf_trace);

        qd_buffer_list_free_buffers(&content->buffers);

        if (content->pending)
            qd_buffer_free(content->pending);

        sys_mutex_free(content->lock);
        sys_atomic_destroy(&content->aborted);
        sys_atomic_destroy(&content->discard);
        sys_atomic_destroy(&content->no_body);
        sys_atomic_destroy(&content->oversize);
        sys_atomic_destroy(&content->priority);
        sys_atomic_destroy(&content->priority_parsed);
        sys_atomic_destroy(&content->receive_complete);
        sys_atomic_destroy(&content->ref_count);
        free_qd_message_content_t(content);
    }

    free_qd_message_t((qd_message_t*) msg);
}


qd_message_t *qd_message_copy(qd_message_t *in_msg)
{
    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    qd_message_content_t *content = msg->content;
    qd_message_pvt_t     *copy    = (qd_message_pvt_t*) new_qd_message_t();

    if (!copy)
        return 0;

    ZERO(copy);

    copy->strip_annotations_in  = msg->strip_annotations_in;

    copy->content = content;

    copy->sent_depth    = QD_DEPTH_NONE;
    copy->cursor.buffer = 0;
    copy->cursor.cursor = 0;
    sys_atomic_init(&copy->send_complete, 0);
    copy->tag_sent      = false;
    copy->is_fanout     = false;

    if (!content->ma_disabled) {
        if (msg->ma_to_override)
            copy->ma_to_override = qd_strdup(msg->ma_to_override);
        copy->ma_filter_trace   = msg->ma_filter_trace;
        copy->ma_filter_ingress = msg->ma_filter_ingress;
        copy->ma_reset_trace    = msg->ma_reset_trace;
        copy->ma_reset_ingress  = msg->ma_reset_ingress;
        copy->ma_phase          = msg->ma_phase;
        copy->ma_streaming      = msg->ma_streaming;
    }

    sys_atomic_inc(&content->ref_count);

    return (qd_message_t*) copy;
}

const char *qd_message_parse_annotations(qd_message_t *in_msg)
{
    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    qd_message_content_t *content = msg->content;

    assert(!content->ma_disabled);  // should not be called when skipping MA processing
    if (content->ma_parsed)
        return 0;
    content->ma_parsed = true;

    content->ma_field_iter_in = qd_message_field_iterator(in_msg, QD_FIELD_MESSAGE_ANNOTATION);
    if (content->ma_field_iter_in == 0)
        return 0;

    qd_parsed_field_t *ma_pf_stream = 0;
    qd_parsed_field_t *ma_pf_phase = 0;
    const char *err = qd_parse_annotations(msg->strip_annotations_in,
                                           content->ma_field_iter_in,
                                           &content->ma_pf_ingress,
                                           &ma_pf_phase,
                                           &content->ma_pf_to_override,
                                           &content->ma_pf_trace,
                                           &ma_pf_stream,
                                           &content->ma_user_annotations,
                                           &content->ma_user_count);
    if (err)
        return(err);

    // cache incoming values into the message

    if (ma_pf_phase) {
        msg->ma_phase = qd_parse_as_int(ma_pf_phase);
        qd_parse_free(ma_pf_phase);
    }

    if (ma_pf_stream) {
        msg->ma_streaming = true;
        qd_parse_free(ma_pf_stream);
    }

    return 0;
}


void qd_message_set_to_override_annotation(qd_message_t *in_msg, const char *to_field)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    free(msg->ma_to_override);
    msg->ma_to_override = to_field ? qd_strdup(to_field) : 0;
}


void qd_message_set_phase_annotation(qd_message_t *in_msg, int phase)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    msg->ma_phase = phase;
}

int qd_message_get_phase_annotation(const qd_message_t *in_msg)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    return msg->ma_phase;
}

void qd_message_set_streaming_annotation(qd_message_t *in_msg)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    msg->ma_streaming = true;
}


void qd_message_disable_router_annotations(qd_message_t *msg)
{
    qd_message_content_t *content = ((qd_message_pvt_t *)msg)->content;
    content->ma_disabled = true;
    content->ma_parsed = true;
}


bool qd_message_is_discard(qd_message_t *msg)
{
    if (!msg)
        return false;
    qd_message_pvt_t *pvt_msg = (qd_message_pvt_t*) msg;
    return IS_ATOMIC_FLAG_SET(&pvt_msg->content->discard);
}

void qd_message_set_discard(qd_message_t *msg, bool discard)
{
    if (!msg)
        return;

    qd_message_pvt_t *pvt_msg = (qd_message_pvt_t*) msg;
    SET_ATOMIC_BOOL(&pvt_msg->content->discard, discard);
}


// update the buffer reference counts for a new outgoing message
//
void qd_message_add_fanout(qd_message_t *out_msg)
{
    assert(out_msg);
    qd_message_pvt_t *msg = (qd_message_pvt_t *)out_msg;
    msg->is_fanout = true;

    qd_message_content_t *content = msg->content;

    LOCK(content->lock);
    ++content->fanout;

    qd_buffer_t *buf = DEQ_HEAD(content->buffers);
    // DISPATCH-1590: content->buffers may not be set up yet if
    // content->pending is the first buffer and it is not yet full.
    if (!buf) {
        assert(content->pending && qd_buffer_size(content->pending) > 0);
        DEQ_INSERT_TAIL(content->buffers, content->pending);
        content->pending = 0;
        buf = DEQ_HEAD(content->buffers);
    }
    // DISPATCH-1330: since we're incrementing the refcount be sure to set
    // the cursor to the head buf in case msg is discarded before all data
    // is sent (we'll decref any unsent buffers at that time)
    //
    msg->cursor.buffer = buf;
    while (buf) {
        qd_buffer_inc_fanout(buf);
        buf = DEQ_NEXT(buf);
    }

    UNLOCK(content->lock);
}


/**
* There are two sources of priority information --
* message and address. Address takes precedence, falling
* through when no address priority has been specified.
* This also means that messages must always have a priority,
* using default value if sender leaves it unspecified.
*/
uint8_t qd_message_get_priority(qd_message_t *msg)
{
    qd_message_content_t *content = MSG_CONTENT(msg);

    if (!IS_ATOMIC_FLAG_SET(&content->priority_parsed))
        qd_message_parse_priority(msg);

    return sys_atomic_get(&content->priority);
}

bool qd_message_receive_complete(qd_message_t *in_msg)
{
    if (!in_msg)
        return false;
    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    return IS_ATOMIC_FLAG_SET(&msg->content->receive_complete);
}


bool qd_message_send_complete(qd_message_t *in_msg)
{
    if (!in_msg)
        return false;

    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    return IS_ATOMIC_FLAG_SET(&msg->send_complete);
}


void qd_message_set_send_complete(qd_message_t *in_msg)
{
    if (!!in_msg) {
        qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
        SET_ATOMIC_FLAG(&msg->send_complete);
    }
}


void qd_message_set_receive_complete(qd_message_t *in_msg)
{
    if (!!in_msg) {
        qd_message_content_t *content = MSG_CONTENT(in_msg);
        qd_message_q2_unblocker_t  q2_unblock = {0};

        LOCK(content->lock);

        SET_ATOMIC_FLAG(&content->receive_complete);
        if (content->q2_input_holdoff) {
            content->q2_input_holdoff = false;
            q2_unblock = content->q2_unblocker;
        }
        content->q2_unblocker.handler = 0;
        qd_nullify_safe_ptr(&content->q2_unblocker.context);

        UNLOCK(content->lock);

        if (q2_unblock.handler)
            q2_unblock.handler(q2_unblock.context);
    }
}

void qd_message_set_no_body(qd_message_t *in_msg)
{
    if (!!in_msg) {
        qd_message_content_t *content = MSG_CONTENT(in_msg);
        SET_ATOMIC_FLAG(&content->no_body);
    }
}

bool qd_message_no_body(qd_message_t *in_msg)
{
    if (!!in_msg) {
        qd_message_content_t *content = MSG_CONTENT(in_msg);
        return IS_ATOMIC_FLAG_SET(&content->no_body);
    }

    return false;
}



bool qd_message_tag_sent(qd_message_t *in_msg)
{
    if (!in_msg)
        return false;

    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    return msg->tag_sent;
}

void qd_message_set_tag_sent(qd_message_t *in_msg, bool tag_sent)
{
    if (!in_msg)
        return;

    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    msg->tag_sent = tag_sent;
}


/**
 * Receive and discard large messages for which there is no destination.
 * Don't waste resources by putting the message into internal buffers.
 * Message locking is not required since the message content buffers are untouched.
 */
qd_message_t *discard_receive(pn_delivery_t *delivery,
                              pn_link_t     *link,
                              qd_message_t  *msg_in)
{
    qd_message_pvt_t *msg  = (qd_message_pvt_t*)msg_in;
    while (1) {
#define DISCARD_BUFFER_SIZE (128 * 1024)
        char dummy[DISCARD_BUFFER_SIZE];
        ssize_t rc = pn_link_recv(link, dummy, DISCARD_BUFFER_SIZE);

        if (rc == 0) {
            // have read all available pn_link incoming bytes
            break;
        } else if (rc == PN_EOS || rc < 0) {
            // End of message or error: finalize message_receive handling
            if (pn_delivery_aborted(delivery)) {
                SET_ATOMIC_FLAG(&msg->content->aborted);
            }
            pn_record_t *record = pn_delivery_attachments(delivery);
            pn_record_set(record, PN_DELIVERY_CTX, 0);
            if (IS_ATOMIC_FLAG_SET(&msg->content->oversize)) {
                // Aborting the content disposes of downstream copies.
                // This has no effect on the received message.
                SET_ATOMIC_FLAG(&msg->content->aborted);
            }
            qd_message_set_receive_complete((qd_message_t*) msg);
            break;
        } else {
            // rc was > 0. bytes were read and discarded.
        }
    }

    return msg_in;
}

qd_message_t * qd_get_message_context(pn_delivery_t *delivery)
{
    pn_record_t *record    = pn_delivery_attachments(delivery);
    if (record)
        return (qd_message_t *) pn_record_get(record, PN_DELIVERY_CTX);

    return 0;
}

bool qd_message_has_data_in_content_or_pending_buffers(qd_message_t   *msg)
{
    if (!msg)
        return false;

    if (MSG_CONTENT(msg)) {
        if (DEQ_SIZE(MSG_CONTENT(msg)->buffers) > 0) {
            qd_buffer_t *buf = DEQ_HEAD(MSG_CONTENT(msg)->buffers);
            if (buf && qd_buffer_size(buf) > 0)
                return true;
        }
        if (MSG_CONTENT(msg)->pending && qd_buffer_size(MSG_CONTENT(msg)->pending) > 0)
            return true;
    }

    return false;
}


qd_message_t *qd_message_receive(pn_delivery_t *delivery)
{
    pn_link_t        *link = pn_delivery_link(delivery);
    qd_link_t       *qdl = (qd_link_t *)pn_link_get_context(link);
    ssize_t           rc;

    pn_record_t *record    = pn_delivery_attachments(delivery);
    qd_message_pvt_t *msg  = (qd_message_pvt_t*) pn_record_get(record, PN_DELIVERY_CTX);

    //
    // If there is no message associated with the delivery then this is the
    // first time we've received anything on this delivery.
    // Allocate a message descriptor and link it and the delivery together.
    //
    if (!msg) {
        msg = (qd_message_pvt_t*) qd_message();
        qd_connection_t *qdc = qd_link_connection(qdl);
        qd_alloc_safe_ptr_t sp = QD_SAFE_PTR_INIT(qdl);
        qd_message_set_q2_unblocked_handler((qd_message_t*) msg, qd_link_q2_restart_receive, sp);
        msg->strip_annotations_in  = qd_connection_strip_annotations_in(qdc);
        pn_record_def(record, PN_DELIVERY_CTX, PN_VOID);
        pn_record_set(record, PN_DELIVERY_CTX, (void*) msg);
        msg->content->max_message_size = qd_connection_max_message_size(qdc);
        qd_link_set_incoming_msg(qdl, (qd_message_t*) msg);
    }

    //
    // The discard flag indicates we should keep reading the input stream
    // but not process the message for delivery.
    // Oversize messages are also discarded.
    //
    if (IS_ATOMIC_FLAG_SET(&msg->content->discard)) {
        return discard_receive(delivery, link, (qd_message_t *)msg);
    }

    // if q2 holdoff has been disabled (disable_q2_holdoff=true), we keep receiving.
    // if q2 holdoff has been enabled (disable_q2_holdoff=false), if input is in holdoff then just exit.
    //      When enough buffers
    //      have been processed and freed by outbound processing then
    //      message holdoff is cleared and receiving may continue.
    //
    LOCK(msg->content->lock);
    if (!qd_link_is_q2_limit_unbounded(qdl) && !msg->content->disable_q2_holdoff) {
        if (msg->content->q2_input_holdoff) {
            UNLOCK(msg->content->lock);
            return (qd_message_t*)msg;
        }
    }
    UNLOCK(msg->content->lock);

    // Loop until msg is complete, error seen, or incoming bytes are consumed
    qd_message_content_t *content = msg->content;
    bool recv_error = false;
    while (1) {
        //
        // handle EOS and clean up after pn receive errors
        //
        bool at_eos = (pn_delivery_partial(delivery) == false) &&
                      (pn_delivery_aborted(delivery) == false) &&
                      (pn_delivery_pending(delivery) == 0);

        if (at_eos || recv_error) {
            // Message is complete
            qd_buffer_t * pending_free = 0; // free empty pending buffer outside of lock
            LOCK(content->lock);
            {
                // Append last buffer if any with data
                if (content->pending) {
                    if (qd_buffer_size(content->pending) > 0) {
                        // pending buffer has bytes that are part of message
                        qd_buffer_set_fanout(content->pending, content->fanout);
                        DEQ_INSERT_TAIL(content->buffers,
                                        content->pending);
                    } else {
                        // pending buffer is empty
                        pending_free = content->pending;
                    }
                    content->pending = 0;
                } else {
                    // pending buffer is absent
                }

                content->receive_complete = true;
                content->q2_unblocker.handler = 0;
                qd_nullify_safe_ptr(&content->q2_unblocker.context);
                if (pn_delivery_aborted(delivery)) {
                    SET_ATOMIC_FLAG(&msg->content->aborted);
                }
                // unlink message and delivery
                pn_record_set(record, PN_DELIVERY_CTX, 0);
            }
            UNLOCK(content->lock);
            if (!!pending_free) {
                qd_buffer_free(pending_free);
            }
            break;
        }

        //
        // Handle a missing or full pending buffer
        //
        if (!content->pending) {
            // Pending buffer is absent: get a new one
            content->pending = qd_buffer();
        } else {
            // Pending buffer exists
            if (qd_buffer_capacity(content->pending) == 0) {
                // Pending buffer is full
                LOCK(content->lock);
                qd_buffer_set_fanout(content->pending, content->fanout);
                DEQ_INSERT_TAIL(content->buffers, content->pending);
                content->pending = 0;
                if (_Q2_holdoff_should_block_LH(content)) {
                    if (!qd_link_is_q2_limit_unbounded(qdl)) {
                        content->q2_input_holdoff = true;
                        UNLOCK(content->lock);
                        break;
                    }
                }
                UNLOCK(content->lock);
                content->pending = qd_buffer();
            } else {
                // Pending buffer still has capacity
            }
        }

        //
        // Try to fill the remaining space in the pending buffer.
        //
        rc = pn_link_recv(link,
                          (char*) qd_buffer_cursor(content->pending),
                          qd_buffer_capacity(content->pending));

        if (rc < 0) {
            // error or eos seen. next pass breaks out of loop
            recv_error = true;
        } else if (rc > 0) {
            //
            // We have received a positive number of bytes for the message.
            // Advance the cursor in the buffer.
            //
            qd_buffer_insert(content->pending, rc);

            // Handle maxMessageSize violations
            if (content->max_message_size) {
                content->bytes_received += rc;
                if (content->bytes_received > content->max_message_size)
                {
                    qd_connection_t *conn = qd_link_connection(qdl);
                    qd_connection_log_policy_denial(qdl, "DENY AMQP Transfer maxMessageSize exceeded");
                    qd_policy_count_max_size_event(link, conn);
                    SET_ATOMIC_FLAG(&content->discard);
                    SET_ATOMIC_FLAG(&content->oversize);
                    return discard_receive(delivery, link, (qd_message_t*)msg);
                }
            }
        } else {
            //
            // We received zero bytes, and no PN_EOS.  This means that we've received
            // all of the data available up to this point, but it does not constitute
            // the entire message.  We'll be back later to finish it up.
            // Return the message so that the caller can start sending out whatever we have received so far
            //
            // push what we do have for testing/processing
            if (qd_buffer_size(content->pending) > 0) {
                LOCK(content->lock);
                qd_buffer_set_fanout(content->pending, content->fanout);
                DEQ_INSERT_TAIL(content->buffers, content->pending);
                content->pending = 0;
                UNLOCK(content->lock);
                content->pending = qd_buffer();
            }
            break;
        }
    }

    return (qd_message_t*) msg;
}


static void send_handler(void *context, const unsigned char *start, int length)
{
    pn_link_t *pnl = (pn_link_t*) context;
    pn_link_send(pnl, (const char*) start, length);
}


// Restore MA to the original user-supplied MA values. This merely sets up the
// annotations section and map header to hold only the user supplied
// annotations.
//
// @return the length of the ma_header field in octets
//
static int restore_user_message_annotations(qd_message_pvt_t *msg, uint8_t *ma_header)
{
    qd_message_content_t *content = msg->content;
    if (content->ma_user_count) {

        // setup the MA descriptor:
        ma_header[0] = 0;
        ma_header[1] = QD_AMQP_SMALLULONG;
        ma_header[2] = QD_PERFORMATIVE_MESSAGE_ANNOTATIONS;

        // setup the MA MAP header.  The type of header (MAP32/8) depends on
        // the size of the map contents.
        const int map_hdr_len = qd_compose_map_header(&ma_header[3],
                                                      content->ma_user_annotations.remaining,
                                                      content->ma_user_count);
        return map_hdr_len + 3;
    }
    return 0;
}


// Generate the MA section header and the router annotations. Any user
// annotations will be sent after ma_header and before ma_trailer.
//
// @return the length of the ma_header field in octets
//
static int compose_router_message_annotations(qd_message_pvt_t *msg, uint8_t *ma_header,
                                              qd_buffer_list_t *ma_trailer)
{
    qd_message_content_t *content = msg->content;

    // account for any user annotations to be sent before the router annotations
    //
    uint32_t mcount = content->ma_user_count;
    uint32_t msize  = content->ma_user_annotations.remaining;

    if (msg->ma_phase) {
        assert(msg->ma_phase < 128); // smallint
        mcount += 2;

        // key:
        msize += QD_MA_PHASE_ENCODED_LEN;
        qd_buffer_list_append(ma_trailer, QD_MA_PHASE_ENCODED, QD_MA_PHASE_ENCODED_LEN);

        // value:
        msize += 2;  // tag + 1 byte value
        uint8_t ma_phase[2];
        ma_phase[0] = QD_AMQP_SMALLINT;
        ma_phase[1] = msg->ma_phase;
        qd_buffer_list_append(ma_trailer, ma_phase, 2);
    }

    if (msg->ma_streaming) {
        mcount += 2;

        // key:
        msize += QD_MA_STREAM_ENCODED_LEN;
        qd_buffer_list_append(ma_trailer, QD_MA_STREAM_ENCODED, QD_MA_STREAM_ENCODED_LEN);

        // value: historically sent as int value 1:
        msize += 2;
        const uint8_t streaming[2] = {QD_AMQP_SMALLINT, 1};
        qd_buffer_list_append(ma_trailer, streaming, 2);
    }

    if (msg->ma_to_override || content->ma_pf_to_override) {
        mcount += 2;

        // key:
        msize += QD_MA_TO_ENCODED_LEN;
        qd_buffer_list_append(ma_trailer, QD_MA_TO_ENCODED, QD_MA_TO_ENCODED_LEN);

        // value: message specific value takes precedence over value in
        // original received message to allow overriding the to-override
        uint8_t hdr[5];  // max length of encoded str8/32 header
        if (msg->ma_to_override) {
            const size_t str_len = strlen(msg->ma_to_override);
            const int hdr_len = qd_compose_str_header(hdr, str_len);

            msize += hdr_len;
            qd_buffer_list_append(ma_trailer, hdr, hdr_len);

            msize += str_len;
            qd_buffer_list_append(ma_trailer, (uint8_t*) msg->ma_to_override, str_len);

        } else {
            qd_buffer_field_t to = qd_parse_value(content->ma_pf_to_override);
            const int hdr_len = qd_compose_str_header(hdr, to.remaining);

            msize += hdr_len;
            qd_buffer_list_append(ma_trailer, hdr, hdr_len);

            msize += to.remaining;
            qd_buffer_list_append_field(ma_trailer, &to);
        }
    }

    if (!msg->ma_filter_ingress) {
        mcount += 2;

        // key
        msize += QD_MA_INGRESS_ENCODED_LEN;
        qd_buffer_list_append(ma_trailer, QD_MA_INGRESS_ENCODED, QD_MA_INGRESS_ENCODED_LEN);

        // value: use original value if present, else the local node is the
        // ingress
        if (content->ma_pf_ingress && !msg->ma_reset_ingress) {
            uint8_t hdr[5];   // max size str8/32 header
            qd_buffer_field_t ingress = qd_parse_value(content->ma_pf_ingress);
            const int hdr_len = qd_compose_str_header(hdr, ingress.remaining);

            msize += hdr_len;
            qd_buffer_list_append(ma_trailer, hdr, hdr_len);

            msize += ingress.remaining;
            qd_buffer_list_append_field(ma_trailer, &ingress);

        } else {
            size_t node_id_len;
            const uint8_t *node_id = qd_router_id_encoded(&node_id_len);
            msize += node_id_len;
            qd_buffer_list_append(ma_trailer, node_id, node_id_len);
        }
    }

    if (!msg->ma_filter_trace) {
        mcount += 2;
        size_t node_id_len;
        const uint8_t *node_id = qd_router_id_encoded(&node_id_len);
        uint32_t trace_count = 1;  // local node
        uint32_t trace_len = node_id_len;
        const bool use_incoming = content->ma_pf_trace && !msg->ma_reset_trace;

        // key
        msize += QD_MA_TRACE_ENCODED_LEN;
        qd_buffer_list_append(ma_trailer, QD_MA_TRACE_ENCODED, QD_MA_TRACE_ENCODED_LEN);

        // value: first compute trace list size and count since the list header
        // must be written first
        qd_buffer_field_t in_trace;
        if (use_incoming) {
            in_trace = qd_parse_value(content->ma_pf_trace);
            trace_len += in_trace.remaining;
            trace_count += qd_parse_sub_count(content->ma_pf_trace);
        }

        uint8_t list_hdr[9];  // max len encoded list header
        const int hdr_len = qd_compose_list_header(list_hdr, trace_len, trace_count);

        msize += hdr_len;
        qd_buffer_list_append(ma_trailer, list_hdr, hdr_len);

        if (use_incoming) {
            msize += in_trace.remaining;
            qd_buffer_list_append_field(ma_trailer, &in_trace);
        }

        msize += node_id_len;
        qd_buffer_list_append(ma_trailer, node_id, node_id_len);
    }

    if (msize) {
        // setup the MA section descriptor:
        ma_header[0] = 0;
        ma_header[1] = QD_AMQP_SMALLULONG;
        ma_header[2] = QD_PERFORMATIVE_MESSAGE_ANNOTATIONS;

        // setup the MA MAP header
        const int hdr_size = qd_compose_map_header(&ma_header[3], msize, mcount);
        return hdr_size + 3;
    }

    return 0;
}


void qd_message_send(qd_message_t *in_msg,
                     qd_link_t    *link,
                     bool          strip_annotations,
                     bool         *q3_stalled)
{
    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    qd_message_content_t *content = msg->content;
    pn_link_t            *pnl     = qd_link_pn(link);

    *q3_stalled                   = false;

    if (msg->sent_depth < QD_DEPTH_MESSAGE_ANNOTATIONS) {

        if (IS_ATOMIC_FLAG_SET(&content->aborted)) {
            // Message is aborted before any part of it has been sent.
            // Declare the message to be sent,
            SET_ATOMIC_FLAG(&msg->send_complete);
            // If the outgoing delivery is not already aborted then abort it.
            if (!pn_delivery_aborted(pn_link_current(pnl))) {
                pn_delivery_abort(pn_link_current(pnl));
            }
            return;
        }

        msg->cursor.buffer = DEQ_HEAD(content->buffers);
        msg->cursor.cursor = qd_buffer_base(msg->cursor.buffer);

        // Since link-routed messages do not set router annotations they will
        // skip the following (content->ma_disabled will be true) and unconditionally
        // start sending from the first octet of the content.

        if (!content->ma_disabled) {
            //
            // Send header if present
            //
            const int header_consume = content->section_message_header.length + content->section_message_header.hdr_length;
            if (header_consume > 0) {
                assert(msg->cursor.cursor == content->section_message_header.offset + qd_buffer_base(msg->cursor.buffer));
                advance_guarded(&msg->cursor.cursor, &msg->cursor.buffer, header_consume, send_handler, (void*) pnl);
            }

            //
            // Send delivery annotation if present
            //
            const int da_consume = content->section_delivery_annotation.length + content->section_delivery_annotation.hdr_length;
            if (da_consume > 0) {
                assert(msg->cursor.cursor == content->section_delivery_annotation.offset + qd_buffer_base(msg->cursor.buffer));
                advance_guarded(&msg->cursor.cursor, &msg->cursor.buffer, da_consume, send_handler, (void*) pnl);
            }

            //
            // Send the message annotations section
            //

            uint8_t ma_header[12];  // max length for MA section and map header
            int ma_header_len;      // size of ma_header content
            qd_buffer_list_t ma_trailer = DEQ_EMPTY;

            if (strip_annotations) {
                // send the original user message annotations only (if present)
                ma_header_len = restore_user_message_annotations(msg, ma_header);
            } else {
                ma_header_len = compose_router_message_annotations(msg, ma_header, &ma_trailer);
            }

            if (ma_header_len) {
                //
                // send annotation section and map header
                //
                pn_link_send(pnl, (char*) ma_header, ma_header_len);

                //
                // Now send any annotation set by the original endpoint
                //
                if (content->ma_user_annotations.remaining) {
                    qd_buffer_t *buf2      = content->ma_user_annotations.buffer;
                    const uint8_t *cursor2 = content->ma_user_annotations.cursor;
                    advance_guarded(&cursor2, &buf2,
                                    content->ma_user_annotations.remaining,
                                    send_handler, (void*) pnl);
                }

                //
                // Next send router annotations
                //
                qd_buffer_t *ta_buf = DEQ_HEAD(ma_trailer);
                while (ta_buf) {
                    char *to_send = (char*) qd_buffer_base(ta_buf);
                    pn_link_send(pnl, to_send, qd_buffer_size(ta_buf));
                    ta_buf = DEQ_NEXT(ta_buf);
                }
                qd_buffer_list_free_buffers(&ma_trailer);
            }

            //
            // Skip over replaced message annotations
            //
            const int ma_consume = content->section_message_annotation.hdr_length + content->section_message_annotation.length;
            if (ma_consume > 0) {
                assert(msg->cursor.cursor == content->section_message_annotation.offset + qd_buffer_base(msg->cursor.buffer));
                advance_guarded(&msg->cursor.cursor, &msg->cursor.buffer, ma_consume, 0, 0);
            }
        }

        msg->sent_depth = QD_DEPTH_MESSAGE_ANNOTATIONS;
    }

    qd_buffer_t *buf = msg->cursor.buffer;

    qd_message_q2_unblocker_t  q2_unblock = {0};
    pn_session_t              *pns        = pn_link_session(pnl);
    const size_t               q3_upper   = BUFFER_SIZE * QD_QLIMIT_Q3_UPPER;

    while (!IS_ATOMIC_FLAG_SET(&content->aborted)
           && buf
           && pn_session_outgoing_bytes(pns) < q3_upper) {

        // This will send the remaining data in the buffer if any. There may be
        // zero bytes left to send if we stopped here last time and there was
        // no next buf
        //
        size_t buf_size = qd_buffer_size(buf);
        int num_bytes_to_send = buf_size - (msg->cursor.cursor - qd_buffer_base(buf));
        ssize_t bytes_sent = 0;
        if (num_bytes_to_send > 0) {
            bytes_sent = pn_link_send(pnl, (const char*)msg->cursor.cursor, num_bytes_to_send);
        }

        LOCK(content->lock);

        if (bytes_sent < 0) {
            //
            // send error - likely the link has failed and we will eventually
            // get a link detach event for this link
            //
            SET_ATOMIC_FLAG(&content->aborted);
            SET_ATOMIC_FLAG(&msg->send_complete);
            if (!pn_delivery_aborted(pn_link_current(pnl))) {
                pn_delivery_abort(pn_link_current(pnl));
            }

            qd_log(qd_message_log_source(),
                   QD_LOG_WARNING,
                   "Sending data on link %s has failed (code=%zi)",
                   pn_link_name(pnl), bytes_sent);

        } else {

            msg->cursor.cursor += bytes_sent;

            if (bytes_sent == num_bytes_to_send) {
                //
                // sent the whole buffer.
                // Can we move to the next buffer?  Only if there is a next buffer
                // or we are at the end and done sending this message
                //
                qd_buffer_t *next_buf = DEQ_NEXT(buf);
                bool complete = qd_message_receive_complete(in_msg);

                if (next_buf || complete) {
                    //
                    // this buffer may be freed if there are no more references to it
                    //
                    uint32_t ref_count = (msg->is_fanout) ? qd_buffer_dec_fanout(buf) : 1;
                    if (ref_count == 1) {

                        DEQ_REMOVE(content->buffers, buf);
                        qd_buffer_free(buf);
                        ++content->buffers_freed;

                        // by freeing a buffer there now may be room to restart a
                        // stalled message receiver
                        if (content->q2_input_holdoff) {
                            if (_Q2_holdoff_should_unblock_LH(content)) {
                                // wake up receive side
                                // Note: clearing holdoff here is easy compared to
                                // clearing it in the deferred callback. Tracing
                                // shows that rx_handler may run and subsequently
                                // set input holdoff before the deferred handler
                                // runs.
                                content->q2_input_holdoff = false;
                                q2_unblock = content->q2_unblocker;
                            }
                        }
                    }   // end free buffer

                    msg->cursor.buffer = next_buf;
                    msg->cursor.cursor = (next_buf) ? qd_buffer_base(next_buf) : 0;

                    SET_ATOMIC_BOOL(&msg->send_complete, (complete && !next_buf));
                }

                buf = next_buf;

            } else if (num_bytes_to_send && bytes_sent == 0) {
                //
                // the proton link cannot take anymore data,
                // retry later...
                //
                buf = 0;
                qd_log(qd_message_log_source(), QD_LOG_DEBUG,
                       "Link %s output limit reached", pn_link_name(pnl));
            }
        }

        UNLOCK(content->lock);
    }

    // the Q2 handler must be invoked outside the lock
    if (q2_unblock.handler)
        q2_unblock.handler(q2_unblock.context);

    if (IS_ATOMIC_FLAG_SET(&content->aborted)) {
        if (pn_link_current(pnl)) {
            SET_ATOMIC_FLAG(&msg->send_complete);
            if (!pn_delivery_aborted(pn_link_current(pnl))) {
                pn_delivery_abort(pn_link_current(pnl));
            }
        }
    }

    *q3_stalled = (pn_session_outgoing_bytes(pns) >= q3_upper);
}


static qd_message_depth_status_t message_check_depth_LH(qd_message_content_t *content,
                                                        qd_message_depth_t    depth,
                                                        const unsigned char  *long_pattern,
                                                        const unsigned char  *short_pattern,
                                                        const unsigned char  *expected_tags,
                                                        qd_field_location_t  *location,
                                                        bool                  optional,
                                                        bool                  protect_buffer)
{
#define LONG  10
#define SHORT 3
    if (depth <= content->parse_depth)
        return QD_MESSAGE_DEPTH_OK;

    qd_section_status_t rc;
    rc = message_section_check_LH(content, &content->parse_buffer, &content->parse_cursor, short_pattern, SHORT, expected_tags, location, false, protect_buffer);
    if (rc == QD_SECTION_NO_MATCH)  // try the alternative
        rc = message_section_check_LH(content, &content->parse_buffer, &content->parse_cursor, long_pattern,  LONG,  expected_tags, location, false, protect_buffer);

    if (rc == QD_SECTION_MATCH || (optional && rc == QD_SECTION_NO_MATCH)) {
        content->parse_depth = depth;
        return QD_MESSAGE_DEPTH_OK;
    }

    if (rc == QD_SECTION_NEED_MORE) {
        if (!IS_ATOMIC_FLAG_SET(&content->receive_complete))
            return QD_MESSAGE_DEPTH_INCOMPLETE;

        // no more data is going to come. OK if at the end and optional:
        if (!can_advance(&content->parse_cursor, &content->parse_buffer) && optional)
            return QD_MESSAGE_DEPTH_OK;

        // otherwise we've got an invalid (truncated) header
    }

    // if QD_SECTION_NO_MATCH && !optional => INVALID;
    // QD_SECTION_INVALID => INVALID;

    return QD_MESSAGE_DEPTH_INVALID;
}


static qd_message_depth_status_t qd_message_check_LH(qd_message_content_t *content, qd_message_depth_t depth)
{
    qd_error_clear();

    if (depth <= content->parse_depth || depth == QD_DEPTH_NONE)
        return QD_MESSAGE_DEPTH_OK; // We've already parsed at least this deep

    // Is there any data to check?  This will also check for null messages, which
    // are not valid:
    //
    qd_buffer_t *buffer  = DEQ_HEAD(content->buffers);
    if (!buffer || qd_buffer_size(buffer) == 0) {
        return IS_ATOMIC_FLAG_SET(&content->receive_complete) ? QD_MESSAGE_DEPTH_INVALID : QD_MESSAGE_DEPTH_INCOMPLETE;
    }

    if (content->buffers_freed) {
        // this is likely a bug: the caller is attempting to access a
        // section after the start of the message has already been sent and
        // released, rendering the parse_buffer/cursor position invalid.
        return QD_MESSAGE_DEPTH_INVALID;
    }

    if (content->parse_buffer == 0) {
        content->parse_buffer = buffer;
        content->parse_cursor = qd_buffer_base(content->parse_buffer);
    }

    qd_message_depth_status_t rc = QD_MESSAGE_DEPTH_OK;
    int last_section = QD_DEPTH_NONE;

    switch (content->parse_depth + 1) {  // start checking at the next unparsed section
    case QD_DEPTH_HEADER:
        //
        // MESSAGE HEADER (optional)
        //
        last_section = QD_DEPTH_HEADER;
        rc = message_check_depth_LH(content, QD_DEPTH_HEADER,
                                    MSG_HDR_LONG, MSG_HDR_SHORT, TAGS_LIST,
                                    &content->section_message_header, true, true);
        if (rc != QD_MESSAGE_DEPTH_OK || depth == QD_DEPTH_HEADER)
            break;

        // fallthrough

    case QD_DEPTH_DELIVERY_ANNOTATIONS:
        //
        // DELIVERY ANNOTATIONS (optional)
        //
        last_section = QD_DEPTH_DELIVERY_ANNOTATIONS;
        rc = message_check_depth_LH(content, QD_DEPTH_DELIVERY_ANNOTATIONS,
                                    DELIVERY_ANNOTATION_LONG, DELIVERY_ANNOTATION_SHORT, TAGS_MAP,
                                    &content->section_delivery_annotation, true, true);
        if (rc != QD_MESSAGE_DEPTH_OK || depth == QD_DEPTH_DELIVERY_ANNOTATIONS)
            break;

        // fallthrough

    case QD_DEPTH_MESSAGE_ANNOTATIONS:
        //
        // MESSAGE ANNOTATION (optional)
        //
        last_section = QD_DEPTH_MESSAGE_ANNOTATIONS;
        rc = message_check_depth_LH(content, QD_DEPTH_MESSAGE_ANNOTATIONS,
                                    MESSAGE_ANNOTATION_LONG, MESSAGE_ANNOTATION_SHORT, TAGS_MAP,
                                    &content->section_message_annotation, true, true);
        if (rc != QD_MESSAGE_DEPTH_OK || depth == QD_DEPTH_MESSAGE_ANNOTATIONS)
            break;

        // fallthough

    case QD_DEPTH_PROPERTIES:
        //
        // PROPERTIES (optional)
        //
        last_section = QD_DEPTH_PROPERTIES;
        rc = message_check_depth_LH(content, QD_DEPTH_PROPERTIES,
                                    PROPERTIES_LONG, PROPERTIES_SHORT, TAGS_LIST,
                                    &content->section_message_properties, true, true);
        if (rc != QD_MESSAGE_DEPTH_OK || depth == QD_DEPTH_PROPERTIES)
            break;

        // fallthrough

    case QD_DEPTH_APPLICATION_PROPERTIES:
        //
        // APPLICATION PROPERTIES (optional)
        //
        last_section = QD_DEPTH_APPLICATION_PROPERTIES;
        rc = message_check_depth_LH(content, QD_DEPTH_APPLICATION_PROPERTIES,
                                    APPLICATION_PROPERTIES_LONG, APPLICATION_PROPERTIES_SHORT, TAGS_MAP,
                                    &content->section_application_properties, true, true);
        if (rc != QD_MESSAGE_DEPTH_OK || depth == QD_DEPTH_APPLICATION_PROPERTIES)
            break;

        // fallthrough

    case QD_DEPTH_BODY:

        //
        // BODY (not optional, but proton allows it - see PROTON-2085)
        //
        // AMQP 1.0 defines 3 valid Body types: Binary, Sequence (list), or Value (any type)
        // Since the body is mandatory, we need to match one of these.  Setting
        // the optional flag to false will force us to check each one until a match is found.
        //
        last_section = QD_DEPTH_BODY;
        rc = message_check_depth_LH(content, QD_DEPTH_BODY,
                                    BODY_VALUE_LONG, BODY_VALUE_SHORT, TAGS_ANY,
                                    &content->section_body, false, false);
        if (rc == QD_MESSAGE_DEPTH_INVALID) {   // may be a different body type, need to check:
            rc = message_check_depth_LH(content, QD_DEPTH_BODY,
                                        BODY_DATA_LONG, BODY_DATA_SHORT, TAGS_BINARY,
                                        &content->section_body, false, false);
            if (rc == QD_MESSAGE_DEPTH_INVALID) {
                rc = message_check_depth_LH(content, QD_DEPTH_BODY,
                                            BODY_SEQUENCE_LONG, BODY_SEQUENCE_SHORT, TAGS_LIST,
                                            &content->section_body, true, false);  // PROTON-2085
            }
        }

        if (rc != QD_MESSAGE_DEPTH_OK || depth == QD_DEPTH_BODY)
            break;

        // fallthrough

    case QD_DEPTH_ALL:
        //
        // FOOTER (optional)
        //
        last_section = QD_DEPTH_ALL;
        rc = message_check_depth_LH(content, QD_DEPTH_ALL,
                                    FOOTER_LONG, FOOTER_SHORT, TAGS_MAP,
                                    &content->section_footer, true, false);
        break;

    default:
        assert(false);  // should not happen!
        qd_error(QD_ERROR_MESSAGE, "BUG! Invalid message depth specified: %d",
                 content->parse_depth + 1);
        return QD_MESSAGE_DEPTH_INVALID;
    }

    if (rc == QD_MESSAGE_DEPTH_INVALID)
        qd_error(QD_ERROR_MESSAGE, "Invalid message: %s section invalid",
                 section_names[last_section]);

    return rc;
}


qd_message_depth_status_t qd_message_check_depth(const qd_message_t *in_msg, qd_message_depth_t depth)
{
    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    qd_message_content_t *content = msg->content;
    qd_message_depth_status_t    result;

    LOCK(content->lock);
    result = qd_message_check_LH(content, depth);
    UNLOCK(content->lock);
    return result;
}


qd_iterator_t *qd_message_field_iterator_typed(qd_message_t *msg, qd_message_field_t field)
{
    qd_field_location_t *loc = qd_message_field_location(msg, field);

    if (!loc)
        return 0;

    if (loc->tag == QD_AMQP_NULL)
        return 0;

    return qd_iterator_buffer(loc->buffer, loc->offset, loc->length + loc->hdr_length, ITER_VIEW_ALL);
}


qd_iterator_t *qd_message_field_iterator(qd_message_t *msg, qd_message_field_t field)
{
    qd_field_location_t *loc = qd_message_field_location(msg, field);

    if (!loc)
        return 0;

    if (loc->tag == QD_AMQP_NULL)
        return 0;

    qd_buffer_t   *buffer = loc->buffer;
    unsigned char *cursor = qd_buffer_base(loc->buffer) + loc->offset;
    if (!advance(&cursor, &buffer, loc->hdr_length))
        return 0;

    return qd_iterator_buffer(buffer, cursor - qd_buffer_base(buffer), loc->length, ITER_VIEW_ALL);
}


ssize_t qd_message_field_length(qd_message_t *msg, qd_message_field_t field)
{
    qd_field_location_t *loc = qd_message_field_location(msg, field);
    if (!loc)
        return -1;

    return loc->length;
}


ssize_t qd_message_field_copy(qd_message_t *msg, qd_message_field_t field, char *buffer, size_t *hdr_length)
{
    qd_field_location_t *loc = qd_message_field_location(msg, field);
    if (!loc)
        return -1;

    qd_buffer_t *buf       = loc->buffer;
    size_t       bufsize   = qd_buffer_size(buf) - loc->offset;
    void        *base      = qd_buffer_base(buf) + loc->offset;
    size_t       remaining = loc->length + loc->hdr_length;
    *hdr_length = loc->hdr_length;

    while (remaining > 0) {
        if (bufsize > remaining)
            bufsize = remaining;
        memcpy(buffer, base, bufsize);
        buffer    += bufsize;
        remaining -= bufsize;
        if (remaining > 0) {
            buf     = buf->next;
            base    = qd_buffer_base(buf);
            bufsize = qd_buffer_size(buf);
        }
    }

    return loc->length + loc->hdr_length;
}


// deprecated - use qd_message_compose() for creating locally generated messages
void qd_message_compose_3(qd_message_t *msg, qd_composed_field_t *field1, qd_composed_field_t *field2, bool receive_complete)
{
    qd_message_content_t *content        = MSG_CONTENT(msg);

    LOCK(content->lock);

    SET_ATOMIC_BOOL(&content->receive_complete, receive_complete);
    qd_buffer_list_t     *field1_buffers = qd_compose_buffers(field1);
    qd_buffer_list_t     *field2_buffers = qd_compose_buffers(field2);

    content->buffers = *field1_buffers;
    DEQ_INIT(*field1_buffers);
    DEQ_APPEND(content->buffers, (*field2_buffers));

    // initialize the Q2 flag:
    if (_Q2_holdoff_should_block_LH(content))
        content->q2_input_holdoff = true;

    UNLOCK(content->lock);

    // set up the locations of the message headers sent prior to the message
    // annotations section.  This is used when composing outgoing router
    // annotations:
    qd_message_parse_annotations(msg);
}


qd_message_t *qd_message_compose(qd_composed_field_t *f1,
                                 qd_composed_field_t *f2,
                                 qd_composed_field_t *f3,
                                 bool receive_complete)
{
    qd_message_t *msg = qd_message();
    if (!msg)
        return 0;

    qd_composed_field_t *fields[4] = {f1, f2, f3, 0};
    qd_message_content_t *content = MSG_CONTENT(msg);
    SET_ATOMIC_BOOL(&content->receive_complete, receive_complete);

    for (int idx = 0; fields[idx] != 0; ++idx) {
        qd_buffer_list_t *bufs = qd_compose_buffers(fields[idx]);
        DEQ_APPEND(content->buffers, (*bufs));
        qd_compose_free(fields[idx]);
    }

    // initialize the Q2 flag:
    if (_Q2_holdoff_should_block_LH(content))
        content->q2_input_holdoff = true;

    // set up the locations of the message headers sent prior to the message
    // annotations section.  This is used when composing outgoing router
    // annotations:
    qd_message_parse_annotations(msg);

    return msg;
}


int qd_message_extend(qd_message_t *msg, qd_composed_field_t *field, bool *q2_blocked)
{
    qd_message_content_t *content = MSG_CONTENT(msg);
    int                   count;
    qd_buffer_list_t     *buffers = qd_compose_buffers(field);
    qd_buffer_t          *buf     = DEQ_HEAD(*buffers);

    if (q2_blocked)
        *q2_blocked = false;

    LOCK(content->lock);
    while (buf) {
        qd_buffer_set_fanout(buf, content->fanout);
        buf = DEQ_NEXT(buf);
    }

    DEQ_APPEND(content->buffers, (*buffers));
    count = DEQ_SIZE(content->buffers);

    // buffers added - must check for Q2:
    if (_Q2_holdoff_should_block_LH(content)) {
        content->q2_input_holdoff = true;
        if (q2_blocked)
            *q2_blocked = true;
    }

    UNLOCK(content->lock);
    return count;
}


/**
 * find_last_buffer_LH
 *
 * Given a field location, find the following:
 *
 *  - *cursor - The pointer to the octet _past_ the last octet in the field.  If this is the last octet in
 *              the buffer, the cursor must point one octet past the buffer.
 *  - *buffer - The last buffer that contains content for this field.
 *
 * Important:  If the last octet of the field is the last octet of a buffer and there are more buffers in the
 * buffer list, *buffer _must_ refer to the buffer that contains the last octet of the field and *cursor must
 * point at the octet following that octet, even if it points past the end of the buffer.
 */
static void find_last_buffer_LH(qd_field_location_t *location, unsigned char **cursor, qd_buffer_t **buffer)
{
    qd_buffer_t *buf       = location->buffer;
    size_t       remaining = location->hdr_length + location->length;

    while (!!buf && remaining > 0) {
        size_t this_buf_size = qd_buffer_size(buf) - (buf == location->buffer ? location->offset : 0);
        if (remaining <= this_buf_size) {
            *buffer = buf;
            *cursor = qd_buffer_base(buf) + (buf == location->buffer ? location->offset : 0) + remaining;
            return;
        }
        remaining -= this_buf_size;
        buf = DEQ_NEXT(buf);
    }

    assert(false);  // The field should already have been validated as complete.
}


void trim_stream_data_headers_LH(qd_message_stream_data_t *stream_data, bool remove_vbin_header)
{
    const qd_field_location_t *location = &stream_data->section;
    qd_buffer_t               *buffer   = location->buffer;
    unsigned char             *cursor   = qd_buffer_base(buffer) + location->offset;

    bool good = advance(&cursor, &buffer, location->hdr_length);
    assert(good);
    if (good) {
        size_t        vbin_hdr_len = 0;
        unsigned char tag          = 0;

        if (remove_vbin_header) {
            vbin_hdr_len = 1;
            // coverity[check_return]
            next_octet(&cursor, &buffer, &tag);
            if (tag == QD_AMQP_VBIN8) {
                advance(&cursor, &buffer, 1);
                vbin_hdr_len += 1;
            } else if (tag == QD_AMQP_VBIN32) {
                advance(&cursor, &buffer, 4);
                vbin_hdr_len += 4;
            }
        }

        // coverity[check_return]
        can_advance(&cursor, &buffer); // bump cursor to the next buffer if necessary

        stream_data->payload.buffer     = buffer;
        stream_data->payload.offset     = cursor - qd_buffer_base(buffer);
        stream_data->payload.length     = location->length - vbin_hdr_len;
        stream_data->payload.hdr_length = 0;
        stream_data->payload.parsed     = true;
        stream_data->payload.tag        = tag;
    }
}


/**
 * qd_message_stream_data_iterator
 *
 * Given a stream_data object, return an iterator that refers to the content of that body data.  This iterator
 * shall not refer to the 3-byte performative header or the header for the vbin{8,32} field.
 *
 * The iterator must be freed eventually by the caller.
 */
qd_iterator_t *qd_message_stream_data_iterator(const qd_message_stream_data_t *stream_data)
{
    const qd_field_location_t *location = &stream_data->payload;

    return qd_iterator_buffer(location->buffer, location->offset, location->length, ITER_VIEW_ALL);
}

/**
 * qd_message_stream_data_payload_length
 *
 * Given a stream_data object, return the length of the payload.
 */
size_t qd_message_stream_data_payload_length(const qd_message_stream_data_t *stream_data)
{
    return stream_data->payload.length;
}


/**
 * qd_message_stream_data_buffer_count
 *
 * Return the number of buffers contained in payload portion of the stream_data object.
 */
int qd_message_stream_data_buffer_count(const qd_message_stream_data_t *stream_data)
{
    if (stream_data->payload.length == 0)
        return 0;

    int count = 1;
    qd_buffer_t *buffer = stream_data->payload.buffer;
    while (!!buffer && buffer != stream_data->last_buffer) {
        buffer = DEQ_NEXT(buffer);
        count++;
    }

    return count;
}


/**
 * qd_message_stream_data_buffers
 *
 * Populate the provided array of pn_raw_buffers with the addresses and lengths of the buffers in the stream_data
 * object.  Don't fill more than count raw_buffers with data.  Start at offset from the zero-th buffer in the
 * stream_data.
 */
int qd_message_stream_data_buffers(qd_message_stream_data_t *stream_data, pn_raw_buffer_t *buffers, int offset, int count)
{
    qd_buffer_t *buffer       = stream_data->payload.buffer;
    size_t       data_offset  = stream_data->payload.offset;
    size_t       payload_len  = stream_data->payload.length;

    qd_message_pvt_t    *owning_message = stream_data->owning_message;


    LOCK(owning_message->content->lock);
    //
    // Skip the buffer offset
    //
    if (offset > 0) {
        assert(offset < qd_message_stream_data_buffer_count(stream_data));
        while (offset > 0 && payload_len > 0) {
            payload_len -= qd_buffer_size(buffer) - data_offset;
            offset--;
            data_offset = 0;
            buffer = DEQ_NEXT(buffer);
        }
    }

    //
    // Fill the buffer array
    //
    int idx = 0;
    while (idx < count && payload_len > 0) {
        size_t buf_size = MIN(payload_len, qd_buffer_size(buffer) - data_offset);
        buffers[idx].context  = 0;  // reserved for use by caller - do not modify!
        buffers[idx].bytes    = (char*) qd_buffer_base(buffer) + data_offset;
        buffers[idx].capacity = BUFFER_SIZE;
        buffers[idx].size     = buf_size;
        buffers[idx].offset   = 0;

        data_offset = 0;
        payload_len -= buf_size;
        buffer = DEQ_NEXT(buffer);
        idx++;
    }
    UNLOCK(owning_message->content->lock);

    return idx;
}

void qd_message_stream_data_release_up_to(qd_message_stream_data_t *stream_data)
{
    if (!stream_data)
        return;

    qd_message_pvt_t         *msg     = stream_data->owning_message;
    qd_message_stream_data_t *next    = DEQ_HEAD(msg->stream_data_list);
    qd_message_stream_data_t *current = NULL;
    while (next && current != stream_data) {
        current = next;
        next = DEQ_NEXT(next);
        qd_message_stream_data_release(current);
    }
}

/**
 * qd_message_stream_data_release
 *
 * Decrement the fanout ref-counts for all of the buffers referred to in the stream_data.  If any have reached zero,
 * remove them from the buffer list and free them.
 *
 * Do not free buffers that overlap with other stream_data or the buffer pointed to by msg->body_buffer.
 */
void qd_message_stream_data_release(qd_message_stream_data_t *stream_data)
{
    if (!stream_data)
        return;

    qd_message_pvt_t     *pvt       = stream_data->owning_message;
    qd_message_content_t *content   = pvt->content;
    qd_buffer_t          *buf;

    //
    // find the range of buffers that do not overlap other stream_data
    // or msg->body_buffer
    //

    qd_buffer_t *start_buf = stream_data->free_prev ? DEQ_PREV(stream_data->section.buffer) : stream_data->section.buffer;
    if (DEQ_PREV(stream_data) && DEQ_PREV(stream_data)->last_buffer == start_buf) {
        // overlap previous stream_data
        if (start_buf == stream_data->last_buffer) {
            // no buffers to free
            DEQ_REMOVE(pvt->stream_data_list, stream_data);
            free_qd_message_stream_data_t(stream_data);
            return;
        }
        start_buf = DEQ_NEXT(start_buf);
    }

    qd_buffer_t *stop_buf;
    if (stream_data->last_buffer == pvt->body_buffer
        || (DEQ_NEXT(stream_data) && DEQ_NEXT(stream_data)->section.buffer == stream_data->last_buffer)) {
        stop_buf = stream_data->last_buffer;
    } else {
        stop_buf = DEQ_NEXT(stream_data->last_buffer);
    }

    LOCK(content->lock);

    bool                      was_blocked = !_Q2_holdoff_should_unblock_LH(content);
    qd_message_q2_unblocker_t q2_unblock  = {0};

    if (pvt->is_fanout) {
        buf = start_buf;
        while (buf != stop_buf) {
            uint32_t old = qd_buffer_dec_fanout(buf);
            (void)old;  // avoid compiler unused var error
            assert(old > 0);
            buf = DEQ_NEXT(buf);
        }
    }

    //
    // Free non-overlapping buffers with zero refcounts.
    //
    buf = start_buf;
    while (buf != stop_buf) {
        qd_buffer_t *next = DEQ_NEXT(buf);
        if (qd_buffer_get_fanout(buf) == 0) {
            DEQ_REMOVE(content->buffers, buf);
            qd_buffer_free(buf);
        }
        buf = next;
    }

    //
    // it is possible that we've freed enough buffers to clear Q2 holdoff
    //
    if (content->q2_input_holdoff
        && was_blocked
        && _Q2_holdoff_should_unblock_LH(content)) {
        content->q2_input_holdoff = false;
        q2_unblock = content->q2_unblocker;
    }

    UNLOCK(content->lock);

    DEQ_REMOVE(pvt->stream_data_list, stream_data);
    free_qd_message_stream_data_t(stream_data);

    if (q2_unblock.handler)
        q2_unblock.handler(q2_unblock.context);
}


qd_message_stream_data_result_t qd_message_next_stream_data(qd_message_t *in_msg, qd_message_stream_data_t **out_stream_data)
{
    qd_message_pvt_t         *msg         = (qd_message_pvt_t*) in_msg;
    qd_message_content_t     *content     = msg->content;
    qd_message_stream_data_t *stream_data = 0;

    *out_stream_data = 0;
    if (!msg->body_cursor) {
        //
        // We haven't returned a body-data record for this message yet.  Start
        // by ensuring the message has been parsed up to the first body section
        //

        qd_message_depth_status_t status = qd_message_check_depth(in_msg, QD_DEPTH_BODY);
        if (status == QD_MESSAGE_DEPTH_OK) {
            // Even if DEPTH_OK, body is optional. If there is no body then move to
            // the footer
            if (msg->content->section_body.buffer) {
                msg->body_buffer = msg->content->section_body.buffer;
                msg->body_cursor = qd_buffer_base(msg->body_buffer) + msg->content->section_body.offset;
            } else {
                // No body. Look for footer
                status = qd_message_check_depth(in_msg, QD_DEPTH_ALL);
                if (status == QD_MESSAGE_DEPTH_OK) {
                    if (msg->content->section_footer.buffer) {
                        // footer is also optional
                        msg->body_buffer = msg->content->section_footer.buffer;
                        msg->body_cursor = qd_buffer_base(msg->body_buffer) + msg->content->section_footer.offset;
                    }
                }
            }
        }

        if (status == QD_MESSAGE_DEPTH_INCOMPLETE)
            return QD_MESSAGE_STREAM_DATA_INCOMPLETE;
        if (status == QD_MESSAGE_DEPTH_INVALID)
            return QD_MESSAGE_STREAM_DATA_INVALID;

        // neither data not footer found
        if (!msg->body_buffer)
            return QD_MESSAGE_STREAM_DATA_NO_MORE;
    }

    // parse out the body data section, or the footer if we're past the
    // last data section

    qd_section_status_t section_status;
    qd_field_location_t location;
    ZERO(&location);

    qd_buffer_t * const old_body_buffer    = msg->body_buffer;
    bool is_footer                         = false;
    qd_message_stream_data_result_t result = QD_MESSAGE_STREAM_DATA_NO_MORE;

    LOCK(content->lock);

    section_status = message_section_check_LH(content,
                                              &msg->body_buffer, &msg->body_cursor,
                                              BODY_DATA_SHORT, 3, TAGS_BINARY,
                                              &location,
                                              true,  // allow duplicates
                                              false);  // do not inc buffer fanout
    if (section_status == QD_SECTION_NO_MATCH) {
        is_footer      = true;
        section_status = message_section_check_LH(content,
                                                  &msg->body_buffer, &msg->body_cursor,
                                                  FOOTER_SHORT, 3, TAGS_MAP,
                                                  &location, true, false);
    }

    switch (section_status) {
    case QD_SECTION_INVALID:
    case QD_SECTION_NO_MATCH:
        result = QD_MESSAGE_STREAM_DATA_INVALID;
        break;

    case QD_SECTION_MATCH:
        stream_data = new_qd_message_stream_data_t();
        ZERO(stream_data);
        stream_data->owning_message = msg;
        stream_data->section        = location;
        find_last_buffer_LH(&stream_data->section, &msg->body_cursor, &msg->body_buffer);
        stream_data->last_buffer = msg->body_buffer;
        trim_stream_data_headers_LH(stream_data, !is_footer);
        DEQ_INSERT_TAIL(msg->stream_data_list, stream_data);
        *out_stream_data = stream_data;

        // if the buffer pointed to by the old msg->body_buffer could not be
        // freed when the previous stream_data was released, release it when
        // this stream_data is released.  Do not free it here as it may affect
        // Q2 threshold, which is checked when the stream_data is released.
        if (DEQ_HEAD(msg->stream_data_list) == stream_data)
            if (old_body_buffer == DEQ_PREV(stream_data->section.buffer))
                stream_data->free_prev = true;

        result = is_footer ? QD_MESSAGE_STREAM_DATA_FOOTER_OK : QD_MESSAGE_STREAM_DATA_BODY_OK;
        break;

    case QD_SECTION_NEED_MORE:
        result = IS_ATOMIC_FLAG_SET(&msg->content->receive_complete) ?
            QD_MESSAGE_STREAM_DATA_NO_MORE : QD_MESSAGE_STREAM_DATA_INCOMPLETE;
        break;
    }

    UNLOCK(content->lock);
    return result;
}


qd_parsed_field_t *qd_message_get_ingress_router(qd_message_t *msg)
{
    return ((qd_message_pvt_t*) msg)->content->ma_pf_ingress;
}


// used by exchange bindings to erase original ingress node id
void qd_message_reset_ingress_router_annotation(qd_message_t *in_msg)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    msg->ma_reset_ingress = true;
}


void qd_message_disable_ingress_router_annotation(qd_message_t *msg)
{
    ((qd_message_pvt_t*) msg)->ma_filter_ingress = true;
}


qd_parsed_field_t *qd_message_get_to_override(qd_message_t *msg)
{
    return ((qd_message_pvt_t*)msg)->content->ma_pf_to_override;
}


qd_parsed_field_t *qd_message_get_trace(qd_message_t *msg)
{
    return ((qd_message_pvt_t*) msg)->content->ma_pf_trace;
}


void qd_message_disable_trace_annotation(qd_message_t *msg)
{
    ((qd_message_pvt_t*) msg)->ma_filter_trace = true;
}


// used by exchange bindings to erase old message trace list
void qd_message_reset_trace_annotation(qd_message_t *in_msg)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    msg->ma_reset_trace = true;
}


int qd_message_is_streaming(const qd_message_t *msg)
{
    const qd_message_pvt_t *msg_pvt = (const qd_message_pvt_t *)msg;
    return msg_pvt->ma_streaming;
}


void qd_message_Q2_holdoff_disable(qd_message_t *msg)
{
    if (!msg)
        return;
    qd_message_pvt_t *msg_pvt = (qd_message_pvt_t*) msg;
    qd_message_content_t *content = msg_pvt->content;
    qd_message_q2_unblocker_t  q2_unblock = {0};

    LOCK(content->lock);
    if (!msg_pvt->content->disable_q2_holdoff) {
        msg_pvt->content->disable_q2_holdoff = true;
        if (content->q2_input_holdoff) {
            content->q2_input_holdoff = false;
            q2_unblock = content->q2_unblocker;
        }
    }
    UNLOCK(content->lock);

    if (q2_unblock.handler)
        q2_unblock.handler(q2_unblock.context);
}


bool _Q2_holdoff_should_block_LH(const qd_message_content_t *content)
{
    const size_t buff_ct = DEQ_SIZE(content->buffers);
    assert(buff_ct >= content->protected_buffers);
    return !content->disable_q2_holdoff && (buff_ct - content->protected_buffers) >= QD_QLIMIT_Q2_UPPER;
}


bool _Q2_holdoff_should_unblock_LH(const qd_message_content_t *content)
{
    const size_t buff_ct = DEQ_SIZE(content->buffers);
    assert(buff_ct >= content->protected_buffers);
    return content->disable_q2_holdoff || (buff_ct - content->protected_buffers) < QD_QLIMIT_Q2_LOWER;
}


bool qd_message_is_Q2_blocked(const qd_message_t *msg)
{
    qd_message_pvt_t     *msg_pvt = (qd_message_pvt_t*) msg;
    qd_message_content_t *content = msg_pvt->content;

    bool blocked;
    LOCK(content->lock);
    blocked = content->q2_input_holdoff;
    UNLOCK(content->lock);
    return blocked;
}


bool qd_message_aborted(const qd_message_t *msg)
{
    assert(msg);
    qd_message_pvt_t * msg_pvt = (qd_message_pvt_t *)msg;
    return IS_ATOMIC_FLAG_SET(&msg_pvt->content->aborted);
}

void qd_message_set_aborted(const qd_message_t *msg)
{
    if (!msg)
        return;
    qd_message_pvt_t * msg_pvt = (qd_message_pvt_t *)msg;
    SET_ATOMIC_FLAG(&msg_pvt->content->aborted);
}


bool qd_message_oversize(const qd_message_t *msg)
{
    qd_message_content_t * mc = MSG_CONTENT(msg);
    return IS_ATOMIC_FLAG_SET(&mc->oversize);
}


int qd_message_stream_data_footer_append(qd_message_t *message, qd_buffer_list_t *footer_props)
{
    qd_composed_field_t *field = 0;
    int rc = 0;

    field = qd_compose(QD_PERFORMATIVE_FOOTER, field);

    // Stick the buffers into the footer compose field.
    qd_compose_insert_binary_buffers(field, footer_props);

    rc = qd_message_extend(message, field, 0);

    qd_compose_free(field);
    return rc;

}

int qd_message_stream_data_append(qd_message_t *message, qd_buffer_list_t *data, bool *q2_blocked)
{
    unsigned int        length = DEQ_SIZE(*data);

    qd_composed_field_t *field = 0;
    int rc = 0;

    if (q2_blocked)
        *q2_blocked = false;

    // DISPATCH-1803: ensure no body data section can exceed the
    // QD_QLIMIT_Q2_LOWER.  This allows the egress router to wait for an entire
    // body data section to arrive and be validated before sending it out to
    // the endpoint without preventing Q2 from being relieved (DISPATCH-2191).
    //
    const size_t buf_limit = QD_QLIMIT_Q2_LOWER - 2;  // reserve 1 extra for performative header
    assert(buf_limit);
    while (length > buf_limit) {
        qd_buffer_t *buf = DEQ_HEAD(*data);
        for (int i = 0; i < buf_limit; ++i) {
            buf = DEQ_NEXT(buf);
        }

        // split the list at buf.  buf becomes head of trailing list

        qd_buffer_list_t trailer = DEQ_EMPTY;
        DEQ_HEAD(trailer) = buf;
        DEQ_TAIL(trailer) = DEQ_TAIL(*data);
        DEQ_TAIL(*data) = DEQ_PREV(buf);
        DEQ_NEXT(DEQ_TAIL(*data)) = 0;
        DEQ_PREV(buf) = 0;
        DEQ_SIZE(trailer) = length - buf_limit;
        DEQ_SIZE(*data) = buf_limit;

        field = qd_compose(QD_PERFORMATIVE_BODY_DATA, field);
        qd_compose_insert_binary_buffers(field, data);

        DEQ_MOVE(trailer, *data);
        length -= buf_limit;
    }

    field = qd_compose(QD_PERFORMATIVE_BODY_DATA, field);
    qd_compose_insert_binary_buffers(field, data);

    rc = qd_message_extend(message, field, q2_blocked);
    qd_compose_free(field);
    return rc;
}


void qd_message_set_q2_unblocked_handler(qd_message_t *msg,
                                         qd_message_q2_unblocked_handler_t callback,
                                         qd_alloc_safe_ptr_t context)
{
    qd_message_content_t *content = MSG_CONTENT(msg);

    LOCK(content->lock);

    content->q2_unblocker.handler = callback;
    content->q2_unblocker.context = context;

    UNLOCK(content->lock);
}


void qd_message_clear_q2_unblocked_handler(qd_message_t *msg)
{
    if (msg) {
        qd_message_content_t *content = MSG_CONTENT(msg);

        LOCK(content->lock);

        content->q2_unblocker.handler = 0;
        qd_nullify_safe_ptr(&content->q2_unblocker.context);

        UNLOCK(content->lock);
    }
}
