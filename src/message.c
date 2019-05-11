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

#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/error.h>
#include <qpid/dispatch/amqp.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/buffer.h>
#include <proton/object.h>
#include "message_private.h"
#include "compose_private.h"
#include "connection_manager_private.h"
#include "aprintf.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>
#include <time.h>
#include <inttypes.h>
#include <assert.h>

#define LOCK   sys_spin_lock
#define UNLOCK sys_spin_unlock

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

PN_HANDLE(PN_DELIVERY_CTX)

ALLOC_DEFINE_CONFIG(qd_message_t, sizeof(qd_message_pvt_t), 0, 0);
ALLOC_DEFINE(qd_message_content_t);

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
//static void format_time(pn_timestamp_t  epoch_time, char *format, char *buffer, size_t len)
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
           char timestamp_bytes[8];
           memset(timestamp_bytes, 0, sizeof(timestamp_bytes));
           char creation_time[100]; //string representation of creation time.
           // 64-bit two’s-complement integer representing milliseconds since the unix epoch
           int timestamp_length = 8;
           pn_timestamp_t creation_timestamp = 0;

           //qd_iterator_t* iter = qd_message_field_iterator(msg, field);
           qd_iterator_t *iter = qd_parse_raw(parsed_field);
           while (!qd_iterator_end(iter) && timestamp_length > 0) {
               timestamp_bytes[--timestamp_length] = qd_iterator_octet(iter);
           }
           memcpy(&creation_timestamp, timestamp_bytes, 8);
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
    if (flags == 0 || !qd_message_check(msg, QD_DEPTH_APPLICATION_PROPERTIES)) {
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
 * Advance cursor through buffer chain by 'consume' bytes.
 * Cursor and buffer args are advanced to point to new position in buffer chain.
 *  - if the number of bytes in the buffer chain is less than or equal to 
 *    the consume number then return a null buffer and cursor.
 *  - the original buffer chain is not changed or freed.
 *
 * @param cursor Pointer into current buffer content
 * @param buffer pointer to current buffer
 * @param consume number of bytes to advance
 */
static void advance(unsigned char **cursor, qd_buffer_t **buffer, int consume)
{
    unsigned char *local_cursor = *cursor;
    qd_buffer_t   *local_buffer = *buffer;

    int remaining = qd_buffer_size(local_buffer) - (local_cursor - qd_buffer_base(local_buffer));
    while (consume > 0) {
        if (consume < remaining) {
            local_cursor += consume;
            consume = 0;
        } else {
            consume -= remaining;
            local_buffer = local_buffer->next;
            if (local_buffer == 0){
                local_cursor = 0;
                break;
            }
            local_cursor = qd_buffer_base(local_buffer);
            remaining = qd_buffer_size(local_buffer) - (local_cursor - qd_buffer_base(local_buffer));
        }
    }

    *cursor = local_cursor;
    *buffer = local_buffer;
}


/**
 * Advance cursor through buffer chain by 'consume' bytes.
 * Cursor and buffer args are advanced to point to new position in buffer chain.
 * Buffer content that is consumed is optionally passed to handler.
 *  - if the number of bytes in the buffer chain is less than or equal to 
 *    the consume number then return the last buffer in the chain
 *    and a cursor pointing t the first unused byte in the buffer.
 *  - the original buffer chain is not changed or freed.
 *
 * @param cursor pointer into current buffer content
 * @param buffer pointer to current buffer
 * @param consume number of bytes to advance
 * @param handler pointer to processor function
 * @param context opaque argument for handler
 */
static void advance_guarded(unsigned char **cursor, qd_buffer_t **buffer, int consume, buffer_process_t handler, void *context)
{
    unsigned char *local_cursor = *cursor;
    qd_buffer_t   *local_buffer = *buffer;

    int remaining = qd_buffer_size(local_buffer) - (local_cursor - qd_buffer_base(local_buffer));
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
            if (!local_buffer->next) {
                local_cursor = qd_buffer_base(local_buffer) + qd_buffer_size(local_buffer);
                break;
            }
            local_buffer = local_buffer->next;
            local_cursor = qd_buffer_base(local_buffer);
            remaining = qd_buffer_size(local_buffer) - (local_cursor - qd_buffer_base(local_buffer));
        }
    }

    *cursor = local_cursor;
    *buffer = local_buffer;
}


static unsigned char next_octet(unsigned char **cursor, qd_buffer_t **buffer)
{
    unsigned char result = **cursor;
    advance(cursor, buffer, 1);
    return result;
}


static int traverse_field(unsigned char **cursor, qd_buffer_t **buffer, qd_field_location_t *field)
{
    qd_buffer_t   *start_buffer = *buffer;
    unsigned char *start_cursor = *cursor;

    unsigned char tag = next_octet(cursor, buffer);
    if (!(*cursor)) return 0;

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
        consume |= ((int) next_octet(cursor, buffer)) << 24;
        if (!(*cursor)) return 0;
        consume |= ((int) next_octet(cursor, buffer)) << 16;
        if (!(*cursor)) return 0;
        consume |= ((int) next_octet(cursor, buffer)) << 8;
        if (!(*cursor)) return 0;
        // Fall through to the next case...

    case 0xA0 :
    case 0xC0 :
    case 0xE0 :
        hdr_length++;
        consume |= (int) next_octet(cursor, buffer);
        if (!(*cursor)) return 0;
        break;
    }

    if (field && !field->parsed) {
        field->buffer     = start_buffer;
        field->offset     = start_cursor - qd_buffer_base(start_buffer);
        field->length     = consume;
        field->hdr_length = hdr_length;
        field->parsed     = true;
        field->tag        = tag;
    }

    advance(cursor, buffer, consume);
    return 1;
}


static int start_list(unsigned char **cursor, qd_buffer_t **buffer)
{
    unsigned char tag = next_octet(cursor, buffer);
    if (!(*cursor)) return 0;
    int length = 0;
    int count  = 0;

    switch (tag) {
    case 0x45 :     // list0
        break;
    case 0xd0 :     // list32
        length |= ((int) next_octet(cursor, buffer)) << 24;
        if (!(*cursor)) return 0;
        length |= ((int) next_octet(cursor, buffer)) << 16;
        if (!(*cursor)) return 0;
        length |= ((int) next_octet(cursor, buffer)) << 8;
        if (!(*cursor)) return 0;
        length |=  (int) next_octet(cursor, buffer);
        if (!(*cursor)) return 0;

        count |= ((int) next_octet(cursor, buffer)) << 24;
        if (!(*cursor)) return 0;
        count |= ((int) next_octet(cursor, buffer)) << 16;
        if (!(*cursor)) return 0;
        count |= ((int) next_octet(cursor, buffer)) << 8;
        if (!(*cursor)) return 0;
        count |=  (int) next_octet(cursor, buffer);
        if (!(*cursor)) return 0;

        break;

    case 0xc0 :     // list8
        length |= (int) next_octet(cursor, buffer);
        if (!(*cursor)) return 0;

        count |= (int) next_octet(cursor, buffer);
        if (!(*cursor)) return 0;
        break;
    }

    return count;
}


//
// Check the buffer chain, starting at cursor to see if it matches the pattern.
// If the pattern matches, check the next tag to see if it's in the set of expected
// tags.  If not, return zero.  If so, set the location descriptor to the good
// tag and advance the cursor (and buffer, if needed) to the end of the matched section.
//
// If there is no match, don't advance the cursor.
//
// Return 0 if the pattern matches but the following tag is unexpected
// Return 0 if the pattern matches and the location already has a pointer (duplicate section)
// Return 1 if the pattern matches and we've advanced the cursor/buffer
// Return 1 if the pattern does not match
//
static int qd_check_and_advance(qd_buffer_t         **buffer,
                                unsigned char       **cursor,
                                const unsigned char  *pattern,
                                int                   pattern_length,
                                const unsigned char  *expected_tags,
                                qd_field_location_t  *location)
{
    qd_buffer_t   *test_buffer = *buffer;
    unsigned char *test_cursor = *cursor;

    if (!test_cursor)
        return 1; // no match

    unsigned char *end_of_buffer = qd_buffer_base(test_buffer) + qd_buffer_size(test_buffer);
    int idx = 0;

    while (idx < pattern_length && *test_cursor == pattern[idx]) {
        idx++;
        test_cursor++;
        if (test_cursor == end_of_buffer) {
            test_buffer = test_buffer->next;
            if (test_buffer == 0)
                return 1; // Pattern didn't match
            test_cursor = qd_buffer_base(test_buffer);
            end_of_buffer = test_cursor + qd_buffer_size(test_buffer);
        }
    }

    if (idx < pattern_length)
        return 1; // Pattern didn't match

    //
    // Pattern matched, check the tag
    //
    while (*expected_tags && *test_cursor != *expected_tags)
        expected_tags++;
    if (*expected_tags == 0)
        return 0;  // Unexpected tag

    if (location->parsed)
        return 0;  // Duplicate section

    //
    // Pattern matched and tag is expected.  Mark the beginning of the section.
    //
    location->buffer     = *buffer;
    location->offset     = *cursor - qd_buffer_base(*buffer);
    location->length     = 0;
    location->hdr_length = pattern_length;

    //
    // Advance the pointers to consume the whole section.
    //
    int pre_consume = 1;  // Count the already extracted tag
    int consume     = 0;
    unsigned char tag = next_octet(&test_cursor, &test_buffer);

    unsigned char tag_subcat = tag & 0xF0;
    if (!test_cursor && tag_subcat != 0x40)
        return 0;

    switch (tag_subcat) {
    case 0x40:               break;
    case 0x50: consume = 1;  break;
    case 0x60: consume = 2;  break;
    case 0x70: consume = 4;  break;
    case 0x80: consume = 8;  break;
    case 0x90: consume = 16; break;

    case 0xB0:
    case 0xD0:
    case 0xF0:
        pre_consume += 3;
        consume |= ((int) next_octet(&test_cursor, &test_buffer)) << 24;
        if (!test_cursor) return 0;
        consume |= ((int) next_octet(&test_cursor, &test_buffer)) << 16;
        if (!test_cursor) return 0;
        consume |= ((int) next_octet(&test_cursor, &test_buffer)) << 8;
        if (!test_cursor) return 0;
        // Fall through to the next case...

    case 0xA0:
    case 0xC0:
    case 0xE0:
        pre_consume += 1;
        consume |= (int) next_octet(&test_cursor, &test_buffer);
        if (!test_cursor) return 0;
        break;
    }

    location->length = pre_consume + consume;
    if (consume)
        advance(&test_cursor, &test_buffer, consume);

    //
    // increment the reference count of the parsed section as location now
    // references it. Note that the cursor has advanced to the octet after the
    // parsed section, so be careful not to include an extra buffer past the
    // end
    //
    qd_buffer_t *start = *buffer;
    qd_buffer_t *last = test_buffer;
    if (last != start && last != 0) {
        if (test_cursor == qd_buffer_base(last)) {
            // last does not include octets for the current section
            last = DEQ_PREV(last);
        }
    }

    while (start) {
        qd_buffer_inc_fanout(start);
        if (start == last)
            break;
        start = DEQ_NEXT(start);
    }

    location->parsed     = 1;

    *cursor = test_cursor;
    *buffer = test_buffer;
    return 1;
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
        (intptr_t) &((qd_message_content_t *)0)->field_message_id,
        (intptr_t) &((qd_message_content_t *)0)->field_user_id,
        (intptr_t) &((qd_message_content_t *)0)->field_to,
        (intptr_t) &((qd_message_content_t *)0)->field_subject,
        (intptr_t) &((qd_message_content_t *)0)->field_reply_to,
        (intptr_t) &((qd_message_content_t *)0)->field_correlation_id,
        (intptr_t) &((qd_message_content_t *)0)->field_content_type,
        (intptr_t) &((qd_message_content_t *)0)->field_content_encoding,
        (intptr_t) &((qd_message_content_t *)0)->field_absolute_expiry_time,
        (intptr_t) &((qd_message_content_t *)0)->field_creation_time,
        (intptr_t) &((qd_message_content_t *)0)->field_group_id,
        (intptr_t) &((qd_message_content_t *)0)->field_group_sequence,
        (intptr_t) &((qd_message_content_t *)0)->field_reply_to_group_id
    };
    // update table above if new fields need to be accessed:
    assert(QD_FIELD_MESSAGE_ID <= field && field <= QD_FIELD_REPLY_TO_GROUP_ID);

    qd_message_content_t *content = MSG_CONTENT(msg);
    if (!content->section_message_properties.parsed) {
        if (!qd_message_check(msg, QD_DEPTH_PROPERTIES) || !content->section_message_properties.parsed)
            return 0;
    }

    const int index = field - QD_FIELD_MESSAGE_ID;
    qd_field_location_t *const location = (qd_field_location_t *)((char *)content + offsets[index]);
    if (location->parsed)
        return location;

    // requested field not parsed out.  Need to parse out up to the requested field:
    qd_buffer_t   *buffer = content->section_message_properties.buffer;
    unsigned char *cursor = qd_buffer_base(buffer) + content->section_message_properties.offset;
    advance(&cursor, &buffer, content->section_message_properties.hdr_length);
    if (index >= start_list(&cursor, &buffer)) return 0;  // properties list too short

    int position = 0;
    while (position < index) {
        qd_field_location_t *f = (qd_field_location_t *)((char *)content + offsets[position]);
        if (f->parsed)
            advance(&cursor, &buffer, f->hdr_length + f->length);
        else // parse it out
            if (!traverse_field(&cursor, &buffer, f)) return 0;
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

    content->priority_parsed  = true;
    content->priority_present = false;

    if (!!iter) {
        qd_parsed_field_t *field = qd_parse(iter);
        if (qd_parse_ok(field)) {
            if (qd_parse_is_list(field) && qd_parse_sub_count(field) >= 2) {
                qd_parsed_field_t *priority_field = qd_parse_sub_value(field, 1);
                if (qd_parse_tag(priority_field) != QD_AMQP_NULL) {
                    uint32_t value = qd_parse_as_uint(priority_field);
                    content->priority = value > QDR_MAX_PRIORITY ? QDR_MAX_PRIORITY : (uint8_t) (value & 0x00ff);
                    content->priority_present = true;
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
            (qd_message_check(msg, QD_DEPTH_HEADER) && content->section_message_header.parsed))
            return &content->section_message_header;
        break;

    case QD_FIELD_PROPERTIES:
        return qd_message_properties_field(msg, field);

    case QD_FIELD_DELIVERY_ANNOTATION:
        if (content->section_delivery_annotation.parsed ||
            (qd_message_check(msg, QD_DEPTH_DELIVERY_ANNOTATIONS) && content->section_delivery_annotation.parsed))
            return &content->section_delivery_annotation;
        break;

    case QD_FIELD_MESSAGE_ANNOTATION:
        if (content->section_message_annotation.parsed ||
            (qd_message_check(msg, QD_DEPTH_MESSAGE_ANNOTATIONS) && content->section_message_annotation.parsed))
            return &content->section_message_annotation;
        break;

    case QD_FIELD_APPLICATION_PROPERTIES:
        if (content->section_application_properties.parsed ||
            (qd_message_check(msg, QD_DEPTH_APPLICATION_PROPERTIES) && content->section_application_properties.parsed))
            return &content->section_application_properties;
        break;

    case QD_FIELD_BODY:
        if (content->section_body.parsed ||
            (qd_message_check(msg, QD_DEPTH_BODY) && content->section_body.parsed))
            return &content->section_body;
        break;

    case QD_FIELD_FOOTER:
        if (content->section_footer.parsed ||
            (qd_message_check(msg, QD_DEPTH_ALL) && content->section_footer.parsed))
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

    DEQ_ITEM_INIT(msg);
    DEQ_INIT(msg->ma_to_override);
    DEQ_INIT(msg->ma_trace);
    DEQ_INIT(msg->ma_ingress);
    msg->ma_phase = 0;
    msg->ma_phase      = 0;
    msg->sent_depth    = QD_DEPTH_NONE;
    msg->cursor.buffer = 0;
    msg->cursor.cursor = 0;
    msg->send_complete = false;
    msg->tag_sent      = false;
    msg->is_fanout     = false;

    msg->content = new_qd_message_content_t();

    if (msg->content == 0) {
        free_qd_message_t((qd_message_t*) msg);
        return 0;
    }

    ZERO(msg->content);
    sys_spin_init(&msg->content->lock);
    sys_atomic_init(&msg->content->ref_count, 1);
    msg->content->parse_depth = QD_DEPTH_NONE;

    return (qd_message_t*) msg;
}


void qd_message_free(qd_message_t *in_msg)
{
    if (!in_msg) return;
    uint32_t rc;
    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;

    qd_buffer_list_free_buffers(&msg->ma_to_override);
    qd_buffer_list_free_buffers(&msg->ma_trace);
    qd_buffer_list_free_buffers(&msg->ma_ingress);

    qd_message_content_t *content = msg->content;

    if (msg->is_fanout) {
        //
        // Adjust the content's fanout count and decrement all buffer fanout
        // counts starting with the msg cursor.  If the buffer count drops to
        // zero we can free it.
        //
        LOCK(&content->lock);

        const bool was_blocked = !qd_message_Q2_holdoff_should_unblock(in_msg);
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
            && qd_message_Q2_holdoff_should_unblock(in_msg)) {

            content->q2_input_holdoff = false;
            qd_link_restart_rx(qd_message_get_receiving_link(in_msg));
        }

        UNLOCK(&content->lock);
    }

    rc = sys_atomic_dec(&content->ref_count) - 1;
    if (rc == 0) {
        if (content->ma_field_iter_in)
            qd_iterator_free(content->ma_field_iter_in);
        if (content->ma_pf_ingress)
            qd_parse_free(content->ma_pf_ingress);
        if (content->ma_pf_phase)
            qd_parse_free(content->ma_pf_phase);
        if (content->ma_pf_to_override)
            qd_parse_free(content->ma_pf_to_override);
        if (content->ma_pf_trace)
            qd_parse_free(content->ma_pf_trace);

        qd_buffer_list_free_buffers(&content->buffers);

        if (content->pending)
            qd_buffer_free(content->pending);

        sys_spin_destroy(&content->lock);
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

    DEQ_ITEM_INIT(copy);
    qd_buffer_list_clone(&copy->ma_to_override, &msg->ma_to_override);
    qd_buffer_list_clone(&copy->ma_trace, &msg->ma_trace);
    qd_buffer_list_clone(&copy->ma_ingress, &msg->ma_ingress);
    copy->ma_phase = msg->ma_phase;
    copy->strip_annotations_in  = msg->strip_annotations_in;

    copy->content = content;

    copy->sent_depth    = QD_DEPTH_NONE;
    copy->cursor.buffer = 0;
    copy->cursor.cursor = 0;
    copy->send_complete = false;
    copy->tag_sent      = false;
    copy->is_fanout     = false;

    qd_message_message_annotations((qd_message_t*) copy);

    sys_atomic_inc(&content->ref_count);

    return (qd_message_t*) copy;
}

void qd_message_message_annotations(qd_message_t *in_msg)
{
    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    qd_message_content_t *content = msg->content;

    if (content->ma_parsed)
        return ;
    content->ma_parsed = true;

    content->ma_field_iter_in = qd_message_field_iterator(in_msg, QD_FIELD_MESSAGE_ANNOTATION);
    if (content->ma_field_iter_in == 0)
        return;

    qd_parse_annotations(
        msg->strip_annotations_in,
        content->ma_field_iter_in,
        &content->ma_pf_ingress,
        &content->ma_pf_phase,
        &content->ma_pf_to_override,
        &content->ma_pf_trace,
        &content->ma_user_annotation_blob,
        &content->ma_count);

    // Construct pseudo-field location of user annotations blob
    // This holds all annotations if no router-specific annotations are present
    if (content->ma_count > 0) {
        qd_field_location_t   *cf  = &content->field_user_annotations;
        qd_iterator_pointer_t *uab = &content->ma_user_annotation_blob;
        cf->buffer = uab->buffer;
        cf->offset = uab->cursor - qd_buffer_base(uab->buffer);
        cf->length = uab->remaining;
        cf->parsed = true;
    }

    // extract phase
    if (content->ma_pf_phase) {
        content->ma_int_phase = qd_parse_as_int(content->ma_pf_phase);
    }

    return;
}


void qd_message_set_trace_annotation(qd_message_t *in_msg, qd_composed_field_t *trace_field)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    qd_buffer_list_free_buffers(&msg->ma_trace);
    qd_compose_take_buffers(trace_field, &msg->ma_trace);
    qd_compose_free(trace_field);
}

void qd_message_set_to_override_annotation(qd_message_t *in_msg, qd_composed_field_t *to_field)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    qd_buffer_list_free_buffers(&msg->ma_to_override);
    qd_compose_take_buffers(to_field, &msg->ma_to_override);
    qd_compose_free(to_field);
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

void qd_message_set_ingress_annotation(qd_message_t *in_msg, qd_composed_field_t *ingress_field)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    qd_buffer_list_free_buffers(&msg->ma_ingress);
    qd_compose_take_buffers(ingress_field, &msg->ma_ingress);
    qd_compose_free(ingress_field);
}

bool qd_message_is_discard(qd_message_t *msg)
{
    if (!msg)
        return false;
    qd_message_pvt_t *pvt_msg = (qd_message_pvt_t*) msg;
    return pvt_msg->content->discard;
}

void qd_message_set_discard(qd_message_t *msg, bool discard)
{
    if (!msg)
        return;

    qd_message_pvt_t *pvt_msg = (qd_message_pvt_t*) msg;
    pvt_msg->content->discard = discard;
}


// update the buffer reference counts for a new outgoing message
//
void qd_message_add_fanout(qd_message_t *in_msg,
                           qd_message_t *out_msg)
{
    if (!out_msg)
        return;

    qd_message_pvt_t *msg = (qd_message_pvt_t *)out_msg;
    msg->is_fanout = true;

    qd_message_content_t *content = msg->content;

    LOCK(&content->lock);
    ++content->fanout;

    // do not free the buffers until all fanout senders are done with them
    qd_buffer_t *buf = DEQ_HEAD(content->buffers);
    if (buf) {
        // DISPATCH-1330: since we're incrementing the refcount be sure to set
        // the cursor to the head buf in case msg is discarded before all data
        // is sent (we'll decref any unsent buffers at that time)
        //
        msg->cursor.buffer = buf;

        while (buf) {
            qd_buffer_inc_fanout(buf);
            buf = DEQ_NEXT(buf);
        }
    }
    UNLOCK(&content->lock);
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

    if (!content->priority_parsed)
        qd_message_parse_priority(msg);

    return content->priority_present ? content->priority : QDR_DEFAULT_PRIORITY;
}

bool qd_message_receive_complete(qd_message_t *in_msg)
{
    if (!in_msg)
        return false;
    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    return msg->content->receive_complete;
}


bool qd_message_send_complete(qd_message_t *in_msg)
{
    if (!in_msg)
        return false;

    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    return msg->send_complete;
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
 * Don't fiddle with locking as no sender is competing with reception.
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
            // end of message or error. Call the message complete
            msg->content->receive_complete = true;
            msg->content->aborted = pn_delivery_aborted(delivery);
            qd_nullify_safe_ptr(&msg->content->input_link_sp);

            pn_record_t *record = pn_delivery_attachments(delivery);
            pn_record_set(record, PN_DELIVERY_CTX, 0);
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
        qd_link_t       *qdl = (qd_link_t *)pn_link_get_context(link);
        qd_connection_t *qdc = qd_link_connection(qdl);
        set_safe_ptr_qd_link_t(pn_link_get_context(link), &msg->content->input_link_sp);
        msg->strip_annotations_in  = qd_connection_strip_annotations_in(qdc);
        pn_record_def(record, PN_DELIVERY_CTX, PN_WEAKREF);
        pn_record_set(record, PN_DELIVERY_CTX, (void*) msg);
    }

    //
    // The discard flag indicates we should keep reading the input stream
    // but not process the message for delivery.
    //
    if (qd_message_is_discard((qd_message_t*)msg)) {
        return discard_receive(delivery, link, (qd_message_t *)msg);
    }

    // if q2 holdoff has been disabled (disable_q2_holdoff=true), we keep receiving.
    // if q2 holdoff has been enabled (disable_q2_holdoff=false), if input is in holdoff then just exit.
    //      When enough buffers
    //      have been processed and freed by outbound processing then
    //      message holdoff is cleared and receiving may continue.
    //
    if (!qd_link_is_q2_limit_unbounded(qdl) && !msg->content->disable_q2_holdoff) {
        if (msg->content->q2_input_holdoff) {
            return (qd_message_t*)msg;
        }
    }

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
            LOCK(&content->lock);
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
                        qd_buffer_free(content->pending);
                    }
                    content->pending = 0;
                } else {
                    // pending buffer is absent
                }

                content->receive_complete = true;
                content->aborted = pn_delivery_aborted(delivery);
                qd_nullify_safe_ptr(&content->input_link_sp);

                // unlink message and delivery
                pn_record_set(record, PN_DELIVERY_CTX, 0);
            }
            UNLOCK(&content->lock);
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
                LOCK(&content->lock);
                qd_buffer_set_fanout(content->pending, content->fanout);
                DEQ_INSERT_TAIL(content->buffers, content->pending);
                content->pending = 0;
                if (qd_message_Q2_holdoff_should_block((qd_message_t *)msg)) {
                    if (!qd_link_is_q2_limit_unbounded(qdl)) {
                        content->q2_input_holdoff = true;
                        UNLOCK(&content->lock);
                        break;
                    }
                }
                UNLOCK(&content->lock);
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
            // We have received a positive number of bytes for the message.  Advance
            // the cursor in the buffer.
            //
            qd_buffer_insert(content->pending, rc);
        } else {
            //
            // We received zero bytes, and no PN_EOS.  This means that we've received
            // all of the data available up to this point, but it does not constitute
            // the entire message.  We'll be back later to finish it up.
            // Return the message so that the caller can start sending out whatever we have received so far
            //
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


static void compose_message_annotations_v0(qd_message_pvt_t *msg, qd_buffer_list_t *out)
{
    if (msg->content->ma_count > 0) {
        qd_composed_field_t *out_ma = qd_compose(QD_PERFORMATIVE_MESSAGE_ANNOTATIONS, 0);

        qd_compose_start_map(out_ma);

        // Bump the map size and count to reflect user's blob.
        // Note that the blob is not inserted here. This code adjusts the
        // size/count of the map that is under construction and the content
        // is inserted by router-node
        qd_compose_insert_opaque_elements(out_ma, msg->content->ma_count,
                                          msg->content->field_user_annotations.length);
        qd_compose_end_map(out_ma);
        qd_compose_take_buffers(out_ma, out);

        qd_compose_free(out_ma);
    }
}


static void compose_message_annotations_v1(qd_message_pvt_t *msg, qd_buffer_list_t *out,
                                           qd_buffer_list_t *out_trailer)
{
    qd_composed_field_t *out_ma = qd_compose(QD_PERFORMATIVE_MESSAGE_ANNOTATIONS, 0);

    bool map_started = false;

    int field_count = 0;
    qd_composed_field_t *field = qd_compose_subfield(0);
    if (!field)
        return;

    // add dispatch router specific annotations if any are defined
    if (!DEQ_IS_EMPTY(msg->ma_to_override) ||
        !DEQ_IS_EMPTY(msg->ma_trace) ||
        !DEQ_IS_EMPTY(msg->ma_ingress) ||
        msg->ma_phase != 0) {

        if (!map_started) {
            qd_compose_start_map(out_ma);
            map_started = true;
        }

        if (!DEQ_IS_EMPTY(msg->ma_to_override)) {
            qd_compose_insert_symbol(field, QD_MA_TO);
            qd_compose_insert_buffers(field, &msg->ma_to_override);
            field_count++;
        }

        if (!DEQ_IS_EMPTY(msg->ma_trace)) {
            qd_compose_insert_symbol(field, QD_MA_TRACE);
            qd_compose_insert_buffers(field, &msg->ma_trace);
            field_count++;
        }

        if (!DEQ_IS_EMPTY(msg->ma_ingress)) {
            qd_compose_insert_symbol(field, QD_MA_INGRESS);
            qd_compose_insert_buffers(field, &msg->ma_ingress);
            field_count++;
        }

        if (msg->ma_phase != 0) {
            qd_compose_insert_symbol(field, QD_MA_PHASE);
            qd_compose_insert_int(field, msg->ma_phase);
            field_count++;
        }
        // pad out to N fields
        for  (; field_count < QD_MA_N_KEYS; field_count++) {
            qd_compose_insert_symbol(field, QD_MA_PREFIX);
            qd_compose_insert_string(field, "X");
        }
    }

    if (msg->content->ma_count > 0) {
        // insert the incoming message user blob
        if (!map_started) {
            qd_compose_start_map(out_ma);
            map_started = true;
        }

        // Bump the map size and count to reflect user's blob.
        // Note that the blob is not inserted here. This code adjusts the
        // size/count of the map that is under construction and the content
        // is inserted by router-node
        qd_compose_insert_opaque_elements(out_ma, msg->content->ma_count,
                                          msg->content->field_user_annotations.length);
    }

    if (field_count > 0) {
        if (!map_started) {
            qd_compose_start_map(out_ma);
            map_started = true;
        }
        qd_compose_insert_opaque_elements(out_ma, field_count * 2,
                                          qd_buffer_list_length(&field->buffers));

    }

    if (map_started) {
        qd_compose_end_map(out_ma);
        qd_compose_take_buffers(out_ma, out);
        qd_compose_take_buffers(field, out_trailer);
    }

    qd_compose_free(out_ma);
    qd_compose_free(field);
}


// create a buffer chain holding the outgoing message annotations section
static void compose_message_annotations(qd_message_pvt_t *msg, qd_buffer_list_t *out,
                                        qd_buffer_list_t *out_trailer,
                                        bool strip_annotations)
{
    if (strip_annotations) {
        compose_message_annotations_v0(msg, out);
    } else {
        compose_message_annotations_v1(msg, out, out_trailer);
    }
}


void qd_message_send(qd_message_t *in_msg,
                     qd_link_t    *link,
                     bool          strip_annotations,
                     bool         *restart_rx,
                     bool         *q3_stalled)
{
    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    qd_message_content_t *content = msg->content;
    qd_buffer_t          *buf     = 0;
    pn_link_t            *pnl     = qd_link_pn(link);

    *restart_rx                   = false;
    *q3_stalled                   = false;

    if (msg->sent_depth < QD_DEPTH_MESSAGE_ANNOTATIONS) {

        if (content->aborted) {
            // Message is aborted before any part of it has been sent.
            // Declare the message to be sent,
            msg->send_complete = true;
            // the link has an outgoing deliver. abort it.
            if (!pn_delivery_aborted(pn_link_current(pnl))) {
                pn_delivery_abort(pn_link_current(pnl));
            }
            return;
        }

        qd_buffer_list_t new_ma;
        qd_buffer_list_t new_ma_trailer;
        DEQ_INIT(new_ma);
        DEQ_INIT(new_ma_trailer);

        // Process  the message annotations if any
        compose_message_annotations(msg, &new_ma, &new_ma_trailer, strip_annotations);

        //
        // Start with the very first buffer;
        //
        buf = DEQ_HEAD(content->buffers);


        //
        // Send header if present
        //
        unsigned char *cursor = qd_buffer_base(buf);
        int header_consume = content->section_message_header.length + content->section_message_header.hdr_length;
        if (content->section_message_header.length > 0) {
            buf    = content->section_message_header.buffer;
            cursor = content->section_message_header.offset + qd_buffer_base(buf);
            advance_guarded(&cursor, &buf, header_consume, send_handler, (void*) pnl);
        }

        //
        // Send delivery annotation if present
        //
        int da_consume = content->section_delivery_annotation.length + content->section_delivery_annotation.hdr_length;
        if (content->section_delivery_annotation.length > 0) {
            buf    = content->section_delivery_annotation.buffer;
            cursor = content->section_delivery_annotation.offset + qd_buffer_base(buf);
            advance_guarded(&cursor, &buf, da_consume, send_handler, (void*) pnl);
        }

        //
        // Send new message annotations map start if any
        //
        qd_buffer_t *da_buf = DEQ_HEAD(new_ma);
        while (da_buf) {
            char *to_send = (char*) qd_buffer_base(da_buf);
            pn_link_send(pnl, to_send, qd_buffer_size(da_buf));
            da_buf = DEQ_NEXT(da_buf);
        }
        qd_buffer_list_free_buffers(&new_ma);

        //
        // Annotations possibly include an opaque blob of user annotations
        //
        if (content->field_user_annotations.length > 0) {
            qd_buffer_t *buf2      = content->field_user_annotations.buffer;
            unsigned char *cursor2 = content->field_user_annotations.offset + qd_buffer_base(buf);
            advance_guarded(&cursor2, &buf2,
                            content->field_user_annotations.length,
                            send_handler, (void*) pnl);
        }

        //
        // Annotations may include the v1 new_ma_trailer
        //
        qd_buffer_t *ta_buf = DEQ_HEAD(new_ma_trailer);
        while (ta_buf) {
            char *to_send = (char*) qd_buffer_base(ta_buf);
            pn_link_send(pnl, to_send, qd_buffer_size(ta_buf));
            ta_buf = DEQ_NEXT(ta_buf);
        }
        qd_buffer_list_free_buffers(&new_ma_trailer);


        //
        // Skip over replaced message annotations
        //
        int ma_consume = content->section_message_annotation.hdr_length + content->section_message_annotation.length;
        if (content->section_message_annotation.length > 0)
            advance_guarded(&cursor, &buf, ma_consume, 0, 0);

        msg->cursor.buffer = buf;

        //
        // If this message has no header and no delivery annotations and no message annotations, set the offset to 0.
        //
        if (header_consume == 0 && da_consume == 0 && ma_consume ==0)
            msg->cursor.cursor = qd_buffer_base(buf);
        else
            msg->cursor.cursor = cursor;

        msg->sent_depth = QD_DEPTH_MESSAGE_ANNOTATIONS;

    }

    buf = msg->cursor.buffer;

    pn_session_t     *pns  = pn_link_session(pnl);

    while (!content->aborted
           && buf
           && pn_session_outgoing_bytes(pns) <= QD_QLIMIT_Q3_UPPER) {

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

        LOCK(&content->lock);

        if (bytes_sent < 0) {
            //
            // send error - likely the link has failed and we will eventually
            // get a link detach event for this link
            //
            content->aborted = true;
            msg->send_complete = true;
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
                            if (qd_message_Q2_holdoff_should_unblock((qd_message_t *)msg)) {
                                // wake up receive side
                                // Note: clearing holdoff here is easy compared to
                                // clearing it in the deferred callback. Tracing
                                // shows that rx_handler may run and subsequently
                                // set input holdoff before the deferred handler
                                // runs.
                                content->q2_input_holdoff = false;
                                *restart_rx = true;
                            }
                        }
                    }   // end free buffer

                    msg->cursor.buffer = next_buf;
                    msg->cursor.cursor = (next_buf) ? qd_buffer_base(next_buf) : 0;

                    msg->send_complete = (complete && !next_buf);
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

        UNLOCK(&content->lock);
    }

    if (content->aborted) {
        if (pn_link_current(pnl)) {
            msg->send_complete = true;
            if (!pn_delivery_aborted(pn_link_current(pnl))) {
                pn_delivery_abort(pn_link_current(pnl));
            }
        }
    }

    *q3_stalled = (pn_session_outgoing_bytes(pns) > QD_QLIMIT_Q3_UPPER);
}


static int qd_check_field_LH(qd_message_content_t *content,
                             qd_message_depth_t    depth,
                             const unsigned char  *long_pattern,
                             const unsigned char  *short_pattern,
                             const unsigned char  *expected_tags,
                             qd_field_location_t  *location,
                             int                   more)
{
#define LONG  10
#define SHORT 3
    if (depth > content->parse_depth) {
        if (0 == qd_check_and_advance(&content->parse_buffer, &content->parse_cursor, long_pattern,  LONG,  expected_tags, location))
            return 0;
        if (0 == qd_check_and_advance(&content->parse_buffer, &content->parse_cursor, short_pattern, SHORT, expected_tags, location))
            return 0;
        if (!more)
            content->parse_depth = depth;
    }
    return 1;
}


static bool qd_message_check_LH(qd_message_content_t *content, qd_message_depth_t depth)
{
    qd_error_clear();

    //
    // In the case of a streaming or multi buffer message, there is a chance that some buffers might be freed before the entire
    // message has arrived in which case we cannot reliably check the message using the depth.
    //
    if (content->buffers_freed)
        return true;

    qd_buffer_t *buffer  = DEQ_HEAD(content->buffers);

    if (!buffer) {
        return false;
    }

    if (depth <= content->parse_depth)
        return true; // We've already parsed at least this deep

    if (content->parse_buffer == 0) {
        content->parse_buffer = buffer;
        content->parse_cursor = qd_buffer_base(content->parse_buffer);
    }

    if (depth == QD_DEPTH_NONE)
        return true;

    //
    // MESSAGE HEADER
    //
    if (0 == qd_check_field_LH(content, QD_DEPTH_HEADER,
                               MSG_HDR_LONG, MSG_HDR_SHORT, TAGS_LIST, &content->section_message_header, 0)) {
        qd_error(QD_ERROR_MESSAGE, "Invalid header");
        return false;
    }
    if (depth == QD_DEPTH_HEADER)
        return true;

    //
    // DELIVERY ANNOTATION
    //
    if (0 == qd_check_field_LH(content, QD_DEPTH_DELIVERY_ANNOTATIONS,
                               DELIVERY_ANNOTATION_LONG, DELIVERY_ANNOTATION_SHORT, TAGS_MAP, &content->section_delivery_annotation, 0)) {
        qd_error(QD_ERROR_MESSAGE, "Invalid delivery-annotations");
        return false;
    }
    if (depth == QD_DEPTH_DELIVERY_ANNOTATIONS)
        return true;

    //
    // MESSAGE ANNOTATION
    //
    if (0 == qd_check_field_LH(content, QD_DEPTH_MESSAGE_ANNOTATIONS,
                               MESSAGE_ANNOTATION_LONG, MESSAGE_ANNOTATION_SHORT, TAGS_MAP, &content->section_message_annotation, 0)) {
        qd_error(QD_ERROR_MESSAGE, "Invalid annotations");
        return false;
    }
    if (depth == QD_DEPTH_MESSAGE_ANNOTATIONS)
        return true;

    //
    // PROPERTIES
    //
    if (0 == qd_check_field_LH(content, QD_DEPTH_PROPERTIES,
                               PROPERTIES_LONG, PROPERTIES_SHORT, TAGS_LIST, &content->section_message_properties, 0)) {
        qd_error(QD_ERROR_MESSAGE, "Invalid message properties");
        return false;
    }
    if (depth == QD_DEPTH_PROPERTIES)
        return true;

    //
    // APPLICATION PROPERTIES
    //
    if (0 == qd_check_field_LH(content, QD_DEPTH_APPLICATION_PROPERTIES,
                               APPLICATION_PROPERTIES_LONG, APPLICATION_PROPERTIES_SHORT, TAGS_MAP, &content->section_application_properties, 0)) {
        qd_error(QD_ERROR_MESSAGE, "Invalid application-properties");
        return false;
    }
    if (depth == QD_DEPTH_APPLICATION_PROPERTIES)
        return true;

    //
    // BODY
    // Note that this function expects a limited set of types in a VALUE section.  This is
    // not a problem for messages passing through Dispatch because through-only messages won't
    // be parsed to BODY-depth.
    //
    if (0 == qd_check_field_LH(content, QD_DEPTH_BODY,
                               BODY_DATA_LONG, BODY_DATA_SHORT, TAGS_BINARY, &content->section_body, 1)) {
        qd_error(QD_ERROR_MESSAGE, "Invalid body data");
        return false;
    }
    if (0 == qd_check_field_LH(content, QD_DEPTH_BODY,
                               BODY_SEQUENCE_LONG, BODY_SEQUENCE_SHORT, TAGS_LIST, &content->section_body, 1)) {
        qd_error(QD_ERROR_MESSAGE, "Invalid body sequence");
        return false;
    }
    if (0 == qd_check_field_LH(content, QD_DEPTH_BODY,
                               BODY_VALUE_LONG, BODY_VALUE_SHORT, TAGS_ANY, &content->section_body, 0)) {
        qd_error(QD_ERROR_MESSAGE, "Invalid body value");
        return false;
    }
    if (depth == QD_DEPTH_BODY)
        return true;

    //
    // FOOTER
    //
    if (0 == qd_check_field_LH(content, QD_DEPTH_ALL,
                               FOOTER_LONG, FOOTER_SHORT, TAGS_MAP, &content->section_footer, 0)) {

        qd_error(QD_ERROR_MESSAGE, "Invalid footer");
        return false;
    }

    return true;
}


int qd_message_check(qd_message_t *in_msg, qd_message_depth_t depth)
{
    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    qd_message_content_t *content = msg->content;
    int                   result;

    LOCK(&content->lock);
    result = qd_message_check_LH(content, depth);
    UNLOCK(&content->lock);
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
    advance(&cursor, &buffer, loc->hdr_length);

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


void qd_message_compose_1(qd_message_t *msg, const char *to, qd_buffer_list_t *buffers)
{
    qd_composed_field_t  *field   = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_message_content_t *content = MSG_CONTENT(msg);
    content->receive_complete     = true;

    qd_compose_start_list(field);
    qd_compose_insert_bool(field, 0);     // durable
    qd_compose_insert_null(field);        // priority
    //qd_compose_insert_null(field);        // ttl
    //qd_compose_insert_boolean(field, 0);  // first-acquirer
    //qd_compose_insert_uint(field, 0);     // delivery-count
    qd_compose_end_list(field);

    qd_buffer_list_t out_ma;
    qd_buffer_list_t out_ma_trailer;
    DEQ_INIT(out_ma);
    DEQ_INIT(out_ma_trailer);
    compose_message_annotations((qd_message_pvt_t*)msg, &out_ma, &out_ma_trailer, false);
    qd_compose_insert_buffers(field, &out_ma);
    // TODO: user annotation blob goes here
    qd_compose_insert_buffers(field, &out_ma_trailer);

    field = qd_compose(QD_PERFORMATIVE_PROPERTIES, field);
    qd_compose_start_list(field);
    qd_compose_insert_null(field);          // message-id
    qd_compose_insert_null(field);          // user-id
    qd_compose_insert_string(field, to);    // to
    //qd_compose_insert_null(field);          // subject
    //qd_compose_insert_null(field);          // reply-to
    //qd_compose_insert_null(field);          // correlation-id
    //qd_compose_insert_null(field);          // content-type
    //qd_compose_insert_null(field);          // content-encoding
    //qd_compose_insert_timestamp(field, 0);  // absolute-expiry-time
    //qd_compose_insert_timestamp(field, 0);  // creation-time
    //qd_compose_insert_null(field);          // group-id
    //qd_compose_insert_uint(field, 0);       // group-sequence
    //qd_compose_insert_null(field);          // reply-to-group-id
    qd_compose_end_list(field);

    if (buffers) {
        field = qd_compose(QD_PERFORMATIVE_BODY_DATA, field);
        qd_compose_insert_binary_buffers(field, buffers);
    }

    qd_compose_take_buffers(field, &content->buffers);
    qd_compose_free(field);
}


void qd_message_compose_2(qd_message_t *msg, qd_composed_field_t *field)
{
    qd_message_content_t *content       = MSG_CONTENT(msg);
    content->receive_complete     = true;

    qd_buffer_list_t     *field_buffers = qd_compose_buffers(field);

    content->buffers = *field_buffers;
    DEQ_INIT(*field_buffers); // Zero out the linkage to the now moved buffers.
}


void qd_message_compose_3(qd_message_t *msg, qd_composed_field_t *field1, qd_composed_field_t *field2)
{
    qd_message_content_t *content        = MSG_CONTENT(msg);
    content->receive_complete     = true;
    qd_buffer_list_t     *field1_buffers = qd_compose_buffers(field1);
    qd_buffer_list_t     *field2_buffers = qd_compose_buffers(field2);

    content->buffers = *field1_buffers;
    DEQ_INIT(*field1_buffers);
    DEQ_APPEND(content->buffers, (*field2_buffers));
}


void qd_message_compose_4(qd_message_t *msg, qd_composed_field_t *field1, qd_composed_field_t *field2, qd_composed_field_t *field3)
{
    qd_message_content_t *content = MSG_CONTENT(msg);
    content->receive_complete     = true;
    qd_buffer_list_t     *field1_buffers = qd_compose_buffers(field1);
    qd_buffer_list_t     *field2_buffers = qd_compose_buffers(field2);
    qd_buffer_list_t     *field3_buffers = qd_compose_buffers(field3);

    content->buffers = *field1_buffers;
    DEQ_INIT(*field1_buffers);
    DEQ_APPEND(content->buffers, (*field2_buffers));
    DEQ_APPEND(content->buffers, (*field3_buffers));
}


qd_parsed_field_t *qd_message_get_ingress    (qd_message_t *msg)
{
    return ((qd_message_pvt_t*)msg)->content->ma_pf_ingress;
}


qd_parsed_field_t *qd_message_get_phase      (qd_message_t *msg)
{
    return ((qd_message_pvt_t*)msg)->content->ma_pf_phase;
}


qd_parsed_field_t *qd_message_get_to_override(qd_message_t *msg)
{
    return ((qd_message_pvt_t*)msg)->content->ma_pf_to_override;
}


qd_parsed_field_t *qd_message_get_trace      (qd_message_t *msg)
{
    return ((qd_message_pvt_t*)msg)->content->ma_pf_trace;
}


int qd_message_get_phase_val(qd_message_t *msg)
{
    return ((qd_message_pvt_t*)msg)->content->ma_int_phase;
}


void qd_message_Q2_holdoff_disable(qd_message_t *msg)
{
    if (!msg)
        return;
    qd_message_pvt_t *msg_pvt = (qd_message_pvt_t*) msg;
    msg_pvt->content->disable_q2_holdoff = true;
}


bool qd_message_Q2_holdoff_should_block(qd_message_t *msg)
{
    if (!msg)
        return false;
    qd_message_pvt_t *msg_pvt = (qd_message_pvt_t*) msg;
    return !msg_pvt->content->disable_q2_holdoff && DEQ_SIZE(msg_pvt->content->buffers) >= QD_QLIMIT_Q2_UPPER;
}


bool qd_message_Q2_holdoff_should_unblock(qd_message_t *msg)
{
    return DEQ_SIZE(((qd_message_pvt_t*)msg)->content->buffers) < QD_QLIMIT_Q2_LOWER;
}


qd_link_t * qd_message_get_receiving_link(const qd_message_t *msg)
{
    return safe_deref_qd_link_t(((qd_message_pvt_t *)msg)->content->input_link_sp);
}


bool qd_message_aborted(const qd_message_t *msg)
{
    return ((qd_message_pvt_t *)msg)->content->aborted;
}

void qd_message_set_aborted(const qd_message_t *msg, bool aborted)
{
    if (!msg)
        return;
    qd_message_pvt_t * msg_pvt = (qd_message_pvt_t *)msg;
    msg_pvt->content->aborted = aborted;
}
