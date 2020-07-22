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

#include "router_core_private.h"
#include <strings.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>

#include <qpid/dispatch/macros.h>


ALLOC_DEFINE(qdr_terminus_t);

const char* QDR_COORDINATOR_ADDRESS = "$coordinator";

qdr_terminus_t *qdr_terminus(pn_terminus_t *pn)
{
    qdr_terminus_t *term = new_qdr_terminus_t();
    ZERO(term);

    term->properties   = pn_data(0);
    term->filter       = pn_data(0);
    term->outcomes     = pn_data(0);
    term->capabilities = pn_data(0);

    if (pn) {
        const char *addr = pn_terminus_get_address(pn);
        if (pn_terminus_get_type(pn) == PN_COORDINATOR) {
            addr = QDR_COORDINATOR_ADDRESS;
            term->coordinator = true;
        }
        if (addr && *addr)
            term->address = qdr_field(addr);

        term->durability        = pn_terminus_get_durability(pn);
        term->expiry_policy     = pn_terminus_get_expiry_policy(pn);
        term->timeout           = pn_terminus_get_timeout(pn);
        term->dynamic           = pn_terminus_is_dynamic(pn);
        term->distribution_mode = pn_terminus_get_distribution_mode(pn);

        pn_data_copy(term->properties,   pn_terminus_properties(pn));
        pn_data_copy(term->filter,       pn_terminus_filter(pn));
        pn_data_copy(term->outcomes,     pn_terminus_outcomes(pn));
        pn_data_copy(term->capabilities, pn_terminus_capabilities(pn));
    }

    return term;
}


void qdr_terminus_free(qdr_terminus_t *term)
{
    if (term == 0)
        return;

    qdr_field_free(term->address);
    pn_data_free(term->properties);
    pn_data_free(term->filter);
    pn_data_free(term->outcomes);
    pn_data_free(term->capabilities);

    free_qdr_terminus_t(term);
}

// DISPATCH-1461: snprintf() is evil - it returns >= size on overflow.  This
// wrapper will never return >= size, even if truncated.  This makes it safe to
// do pointer & length arithmetic without overflowing the destination buffer in
// qdr_terminus_format()
STATIC INLINE size_t safe_snprintf(char *str, size_t size, const char *format, ...) {
    // max size allowed must be INT_MAX (since vsnprintf returns an int)
    if (size == 0 || size > INT_MAX) {
        return 0;
    }
    int max_possible_return_value = (int)(size - 1);
    va_list ap;
    va_start(ap, format);
    int rc = vsnprintf(str, size, format, ap);
    va_end(ap);

    if (rc < 0) {
        if (size > 0 && str) {
            *str = 0;
        }
        return 0;
    }

    if (rc > max_possible_return_value) {
        rc = max_possible_return_value;
    }
    return (size_t)rc;
}

void qdr_terminus_format(qdr_terminus_t *term, char *output, size_t *size)
{
    size_t len = safe_snprintf(output, *size, "{");

    output += len;
    *size  -= len;
    len     = 0;

    do {
        if (term == 0)
            break;

        if (term->coordinator) {
            len = safe_snprintf(output, *size, "<coordinator>");
            break;
        }

        if (term->dynamic)
            len = safe_snprintf(output, *size, "<dynamic>");
        else if (term->address && term->address->iterator) {
            qd_iterator_reset_view(term->address->iterator, ITER_VIEW_ALL);
            len = qd_iterator_ncopy(term->address->iterator, (unsigned char*) output, *size - 1);
            output[len] = 0;
        } else if (term->address == 0)
            len = safe_snprintf(output, *size, "<none>");

        output += len;
        *size  -= len;

        char *text = "";
        switch (term->durability) {
        case PN_NONDURABLE:                              break;
        case PN_CONFIGURATION: text = " dur:config";     break;
        case PN_DELIVERIES:    text = " dur:deliveries"; break;
        }

        len     = safe_snprintf(output, *size, "%s", text);
        output += len;
        *size  -= len;

        switch (term->expiry_policy) {
        case PN_EXPIRE_WITH_LINK:       text = " expire:link";  break;
        case PN_EXPIRE_WITH_SESSION:    text = " expire:sess";  break;
        case PN_EXPIRE_WITH_CONNECTION: text = " expire:conn";  break;
        case PN_EXPIRE_NEVER:           text = "";              break;
        }

        len     = safe_snprintf(output, *size, "%s", text);
        output += len;
        *size  -= len;

        switch (term->distribution_mode) {
        case PN_DIST_MODE_UNSPECIFIED: text = "";             break;
        case PN_DIST_MODE_COPY:        text = " dist:copy";   break;
        case PN_DIST_MODE_MOVE:        text = " dist:move";   break;
        }

        len     = safe_snprintf(output, *size, "%s", text);
        output += len;
        *size  -= len;

        if (term->timeout > 0) {
            len     = safe_snprintf(output, *size, " timeout:%"PRIu32, term->timeout);
            output += len;
            *size  -= len;
        }

        if (term->capabilities && pn_data_size(term->capabilities) > 0) {
            len     = safe_snprintf(output, *size, " caps:");
            output += len;
            *size  -= len;

            len = *size;
            pn_data_format(term->capabilities, output, &len);
            output += len;
            *size  -= len;
        }

        if (term->filter && pn_data_size(term->filter) > 0) {
            len     = safe_snprintf(output, *size, " flt:");
            output += len;
            *size  -= len;

            len = *size;
            pn_data_format(term->filter, output, &len);
            output += len;
            *size  -= len;
        }

        if (term->outcomes && pn_data_size(term->outcomes) > 0) {
            len     = safe_snprintf(output, *size, " outcomes:");
            output += len;
            *size  -= len;

            len = *size;
            pn_data_format(term->outcomes, output, &len);
            output += len;
            *size  -= len;
        }

        if (term->properties && pn_data_size(term->properties) > 0) {
            len     = safe_snprintf(output, *size, " props:");
            output += len;
            *size  -= len;

            len = *size;
            pn_data_format(term->properties, output, &len);
            output += len;
            *size  -= len;
        }

        len = 0;
    } while (false);

    output += len;
    *size  -= len;

    len = safe_snprintf(output, *size, "}");
    *size -= len;
}


void qdr_terminus_copy(qdr_terminus_t *from, pn_terminus_t *to)
{
    if (!from) {
        pn_terminus_set_type(to, PN_UNSPECIFIED);
        return;
    }

    if (from->coordinator) {
        pn_terminus_set_type(to, PN_COORDINATOR);
        pn_data_copy(pn_terminus_capabilities(to), from->capabilities);
    } else {
        if (from->address) {
            qd_iterator_reset_view(from->address->iterator, ITER_VIEW_ALL);
            unsigned char *addr = qd_iterator_copy(from->address->iterator);
            pn_terminus_set_address(to, (char*) addr);
            free(addr);
        }

        pn_terminus_set_durability(to,        from->durability);
        pn_terminus_set_expiry_policy(to,     from->expiry_policy);
        pn_terminus_set_timeout(to,           from->timeout);
        pn_terminus_set_dynamic(to,           from->dynamic);
        pn_terminus_set_distribution_mode(to, from->distribution_mode);

        pn_data_copy(pn_terminus_properties(to),   from->properties);
        pn_data_copy(pn_terminus_filter(to),       from->filter);
        pn_data_copy(pn_terminus_outcomes(to),     from->outcomes);
        pn_data_copy(pn_terminus_capabilities(to), from->capabilities);
    }
}


void qdr_terminus_add_capability(qdr_terminus_t *term, const char *capability)
{
    pn_data_put_symbol(term->capabilities, pn_bytes(strlen(capability), capability));
}


bool qdr_terminus_has_capability(qdr_terminus_t *term, const char *capability)
{
    pn_data_t *cap = term->capabilities;
    pn_data_rewind(cap);
    pn_data_next(cap);
    if (cap && pn_data_type(cap) == PN_SYMBOL) {
        pn_bytes_t sym = pn_data_get_symbol(cap);
        if (sym.size == strlen(capability) && strcmp(sym.start, capability) == 0)
            return true;
    }

    return false;
}

static int get_waypoint_ordinal(pn_data_t* cap)
{
    if (pn_data_type(cap) == PN_SYMBOL) {
        pn_bytes_t sym = pn_data_get_symbol(cap);
        size_t     len = strlen(QD_CAPABILITY_WAYPOINT_DEFAULT);
        if (sym.size >= len && strncmp(sym.start, QD_CAPABILITY_WAYPOINT_DEFAULT, len) == 0) {
            if (sym.size == len)
                return 1;    // This is the default ordinal
            if (sym.size == len + 2 && sym.start[len + 1] > '0' && sym.start[len + 1] <= '9')
                return (int) (sym.start[len + 1] - '0');
        }
    }
    return 0;
}


int qdr_terminus_waypoint_capability(qdr_terminus_t *term)
{
    pn_data_t *cap = term->capabilities;
    pn_data_rewind(cap);
    pn_data_next(cap);
    if (cap) {
        int ordinal = get_waypoint_ordinal(cap);
        if (ordinal) {
            return ordinal;
        } else if (pn_data_type(cap) == PN_ARRAY && pn_data_enter(cap)) {
            while (pn_data_next(cap)) {
                ordinal = get_waypoint_ordinal(cap);
                if (ordinal) {
                    return ordinal;
                }
            }
        }
    }

    return 0;
}


bool qdr_terminus_is_anonymous(qdr_terminus_t *term)
{
    return term == 0 || (term->address == 0 && !term->dynamic);
}

bool qdr_terminus_is_coordinator(qdr_terminus_t *term)
{
    return term->coordinator;
}


bool qdr_terminus_is_dynamic(qdr_terminus_t *term)
{
    return term->dynamic;
}

bool qdr_terminus_survives_disconnect(qdr_terminus_t *term)
{
    return term->timeout > 0 || term->expiry_policy == PN_EXPIRE_NEVER;
}

void qdr_terminus_set_address(qdr_terminus_t *term, const char *addr)
{
    qdr_field_free(term->address);
    term->address = qdr_field(addr);
}


void qdr_terminus_set_address_iterator(qdr_terminus_t *term, qd_iterator_t *addr)
{
    qdr_field_t *old = term->address;
    term->address = qdr_field_from_iter(addr);
    qdr_field_free(old);
}


qd_iterator_t *qdr_terminus_get_address(qdr_terminus_t *term)
{
    if (term->address == 0)
        return 0;

    return term->address->iterator;
}

void qdr_terminus_insert_address_prefix(qdr_terminus_t *term, const char *prefix)
{
    qd_iterator_t *orig = qdr_terminus_get_address(term);
    char *rewrite_addr = 0;

    size_t prefix_len = strlen(prefix);
    size_t orig_len = qd_iterator_length(orig);
    rewrite_addr = malloc(prefix_len + orig_len + 1);
    strcpy(rewrite_addr, prefix);
    qd_iterator_strncpy(orig, rewrite_addr+prefix_len, orig_len + 1);

    qdr_terminus_set_address(term, rewrite_addr);
    free(rewrite_addr);
}

void qdr_terminus_strip_address_prefix(qdr_terminus_t *term, const char *prefix)
{
    qd_iterator_t *orig = qdr_terminus_get_address(term);
    size_t prefix_len = strlen(prefix);
    size_t orig_len = qd_iterator_length(orig);
    if (orig_len > prefix_len && qd_iterator_prefix(orig, prefix)) {
        char *rewrite_addr = malloc(orig_len + 1);
        qd_iterator_strncpy(orig, rewrite_addr, orig_len + 1);
        qdr_terminus_set_address(term, rewrite_addr + prefix_len);
        free(rewrite_addr);
    }
}


qd_iterator_t *qdr_terminus_dnp_address(qdr_terminus_t *term)
{
    pn_data_t *props = term->properties;
    if (!props)
        return 0;

    pn_data_rewind(props);
    if (pn_data_next(props) && pn_data_enter(props) && pn_data_next(props)) {
        pn_bytes_t sym = pn_data_get_symbol(props);
        if (sym.start && strcmp(QD_DYNAMIC_NODE_PROPERTY_ADDRESS, sym.start) == 0) {
            if (pn_data_next(props)) {
                pn_bytes_t val = pn_data_get_string(props);
                if (val.start && *val.start != '\0')
                    return qd_iterator_binary(val.start, val.size, ITER_VIEW_ALL);
            }
        }
    }

    return 0;
}


void qdr_terminus_set_dnp_address_iterator(qdr_terminus_t *term, qd_iterator_t *iter)
{
    char       buffer[1001];
    char      *text    = buffer;
    bool       on_heap = false;
    pn_data_t *old     = term->properties;
    size_t     len;

    if (!old)
        return;

    if (qd_iterator_length(iter) < 1000) {
        len = qd_iterator_ncopy(iter, (unsigned char*) text, 1000);
        text[len] = '\0';
    } else {
        text    = (char*) qd_iterator_copy(iter);
        on_heap = true;
        len = strlen(text);
    }

    pn_data_t *new = pn_data(pn_data_size(old));
    pn_data_put_map(new);
    pn_data_enter(new);
    pn_data_put_symbol(new, pn_bytes(strlen(QD_DYNAMIC_NODE_PROPERTY_ADDRESS), QD_DYNAMIC_NODE_PROPERTY_ADDRESS));
    pn_data_put_string(new, pn_bytes(len, text));
    pn_data_exit(new);

    term->properties = new;
    pn_data_free(old);

    if (on_heap)
        free(text);
}

