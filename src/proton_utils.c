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

#include "qpid/dispatch/proton_utils.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

char *qdpn_data_as_string(pn_data_t *data)
{
#define MAX_BUFFER 50
    char buffer[MAX_BUFFER + 1];
    buffer[MAX_BUFFER] = '\0';

    switch(pn_data_type(data)) {
    case PN_BOOL:
        return strdup(pn_data_get_bool(data) ? "true" : "false");

    case PN_UBYTE:
        snprintf(buffer, MAX_BUFFER, "%"PRId8, pn_data_get_ubyte(data));
        return strdup(buffer);

    case PN_BYTE:
        snprintf(buffer, MAX_BUFFER, "%"PRId8, pn_data_get_byte(data));
        return strdup(buffer);

    case PN_USHORT:
        snprintf(buffer, MAX_BUFFER, "%"PRId16, pn_data_get_ushort(data));
        return strdup(buffer);

    case PN_SHORT:
        snprintf(buffer, MAX_BUFFER, "%"PRId16, pn_data_get_short(data));
        return strdup(buffer);

    case PN_UINT:
        snprintf(buffer, MAX_BUFFER, "%"PRId32, pn_data_get_uint(data));
        return strdup(buffer);

    case PN_INT:
        snprintf(buffer, MAX_BUFFER, "%"PRId32, pn_data_get_int(data));
        return strdup(buffer);

    case PN_CHAR: {
        char c = (char) pn_data_get_char(data);
        return strndup(&c, 1);
    }

    case PN_ULONG:
        snprintf(buffer, MAX_BUFFER, "%"PRId64, pn_data_get_ulong(data));
        return strdup(buffer);

    case PN_LONG:
        snprintf(buffer, MAX_BUFFER, "%"PRId64, pn_data_get_long(data));
        return strdup(buffer);

    case PN_TIMESTAMP: {
#if _POSIX_C_SOURCE || _BSD_SOURCE || _SVID_SOURCE
        time_t t = (time_t) (pn_data_get_timestamp(data));
        ctime_r(&t, buffer);
        size_t len = strlen(buffer);
        if (buffer[len - 1] == '\n')
            buffer[len - 1] = '\0';
        return strdup(buffer);
#else
        snprintf(buffer, MAX_BUFFER, "%"PRId64, pn_data_get_timestamp(data));
        return strdup(buffer);
#endif
    }

    case PN_FLOAT:
        snprintf(buffer, MAX_BUFFER, "%lg", pn_data_get_float(data));
        return strdup(buffer);

    case PN_DOUBLE:
        snprintf(buffer, MAX_BUFFER, "%lg", pn_data_get_double(data));
        return strdup(buffer);

    case PN_DECIMAL32:
        snprintf(buffer, MAX_BUFFER, "%"PRId32, pn_data_get_decimal32(data));
        return strdup(buffer);

    case PN_DECIMAL64:
        snprintf(buffer, MAX_BUFFER, "%"PRId64, pn_data_get_decimal64(data));
        return strdup(buffer);

    case PN_UUID: {
        pn_uuid_t uuid = pn_data_get_uuid(data);
        uint8_t  *u    = (uint8_t*) uuid.bytes;
        snprintf(buffer, MAX_BUFFER,
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 u[0], u[1], u[2],  u[3],  u[4],  u[5],  u[6],  u[7],
                 u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
        return strdup(buffer);
    }

    case PN_BINARY: {
        pn_bytes_t bytes = pn_data_get_binary(data);

        //
        // Search the binary field for unprintable characters.  If found, don't return a
        // string representation.
        //
        for (int i = 0; i < bytes.size; i++) {
            if (!isprint(bytes.start[i]))
                return 0;
        }

        return strndup(bytes.start, bytes.size);
    }

    case PN_STRING:
        return strdup(pn_data_get_string(data).start);

    case PN_SYMBOL:
        return strdup(pn_data_get_symbol(data).start);

    //
    // Return null for the following types:
    //
    case PN_DECIMAL128:
    case PN_DESCRIBED:
    case PN_ARRAY:
    case PN_LIST:
    case PN_MAP:
    case PN_NULL:
    default:
        break;
    }

    return 0;
}



int qdpn_data_insert(pn_data_t *dest, pn_data_t *src)
{
    assert(dest && src);

    switch (pn_data_type(src)) {

        // simple scalar types

    case PN_NULL:
        pn_data_put_null(dest);
        break;
    case PN_BOOL:
        pn_data_put_bool(dest, pn_data_get_bool(src));
        break;
    case PN_UBYTE:
        pn_data_put_ubyte(dest, pn_data_get_ubyte(src));
        break;
    case PN_BYTE:
        pn_data_put_byte(dest, pn_data_get_byte(src));
        break;
    case PN_USHORT:
        pn_data_put_ushort(dest, pn_data_get_ushort(src));
        break;
    case PN_SHORT:
        pn_data_put_short(dest, pn_data_get_short(src));
        break;
    case PN_UINT:
        pn_data_put_uint(dest, pn_data_get_uint(src));
        break;
    case PN_INT:
        pn_data_put_int(dest, pn_data_get_int(src));
        break;
    case PN_CHAR:
        pn_data_put_char(dest, pn_data_get_char(src));
        break;
    case PN_ULONG:
        pn_data_put_ulong(dest, pn_data_get_ulong(src));
        break;
    case PN_LONG:
        pn_data_put_long(dest, pn_data_get_long(src));
        break;
    case PN_TIMESTAMP:
        pn_data_put_timestamp(dest, pn_data_get_timestamp(src));
        break;
    case PN_FLOAT:
        pn_data_put_float(dest, pn_data_get_float(src));
        break;
    case PN_DOUBLE:
        pn_data_put_double(dest, pn_data_get_double(src));
        break;
    case PN_DECIMAL32:
        pn_data_put_decimal32(dest, pn_data_get_decimal32(src));
        break;
    case PN_DECIMAL64:
        pn_data_put_decimal64(dest, pn_data_get_decimal64(src));
        break;
    case PN_DECIMAL128:
        pn_data_put_decimal128(dest, pn_data_get_decimal128(src));
        break;
    case PN_UUID:
        pn_data_put_uuid(dest, pn_data_get_uuid(src));
        break;
    case PN_BINARY:
        pn_data_put_binary(dest, pn_data_get_binary(src));
        break;
    case PN_STRING:
        pn_data_put_string(dest, pn_data_get_string(src));
        break;
    case PN_SYMBOL:
        pn_data_put_symbol(dest, pn_data_get_symbol(src));
        break;

        // complex types

    case PN_DESCRIBED:
        // two children: descriptor and value:
        pn_data_put_described(dest);
        pn_data_enter(dest);
        pn_data_enter(src);
        pn_data_next(src);
        qdpn_data_insert(dest, src);
        pn_data_next(src);
        qdpn_data_insert(dest, src);
        pn_data_exit(src);
        pn_data_exit(dest);
        break;

    case PN_ARRAY: {
        const size_t count = pn_data_get_array(src);
        const bool described = pn_data_is_array_described(src);
        const pn_type_t atype = pn_data_get_array_type(src);

        pn_data_put_array(dest, described, atype);
        pn_data_enter(dest);
        pn_data_enter(src);
        if (described) {
            pn_data_next(src);
            qdpn_data_insert(dest, src);
        }

        for (size_t i = 0; i < count; ++i) {
            pn_data_next(src);
            qdpn_data_insert(dest, src);
        }
        pn_data_exit(src);
        pn_data_exit(dest);
    } break;

    case PN_LIST: {
        const size_t count = pn_data_get_list(src);

        pn_data_put_list(dest);
        pn_data_enter(dest);
        pn_data_enter(src);
        for (size_t i = 0; i < count; ++i) {
            pn_data_next(src);
            qdpn_data_insert(dest, src);
        }
        pn_data_exit(src);
        pn_data_exit(dest);
    } break;

    case PN_MAP: {
        const size_t count = pn_data_get_map(src);

        pn_data_put_map(dest);
        pn_data_enter(dest);
        pn_data_enter(src);
        for (size_t i = 0; i < count / 2; ++i) {
            // key
            pn_data_next(src);
            qdpn_data_insert(dest, src);
            // value
            pn_data_next(src);
            qdpn_data_insert(dest, src);
        }
        pn_data_exit(src);
        pn_data_exit(dest);

    } break;

    default:
        break;
    }
    return 0;
}
