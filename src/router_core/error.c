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


// The AMQP 'error' type
//
// The AMQP standard defines an 'error' type for expressing various formal
// errors. Corresponds to a Proton Condition (pn_condition_t) that is associate
// with the Proton Disposition (pn_disposition_t).
//
struct qdr_error_t {
    qdr_field_t *name;         // symbol, e.g. "amqp:connection:forced"
    qdr_field_t *description;  // string
    pn_data_t   *info;         // symbol-keyed map
};

ALLOC_DECLARE(qdr_error_t);
ALLOC_DEFINE(qdr_error_t);

qdr_error_t *qdr_error_from_pn(pn_condition_t *pn)
{
    if (!pn)
        return 0;

    qdr_error_t *error = 0;

    const char *name = pn_condition_get_name(pn);
    const char *desc = pn_condition_get_description(pn);
    pn_data_t *info = pn_condition_info(pn);
    const bool info_ok = (info && pn_data_size(info) > 0);

    if ((name && *name) || (desc && *desc) || info_ok) {
        error = new_qdr_error_t();
        ZERO(error);

        if (name && *name)
            error->name = qdr_field(name);

        if (desc && *desc)
            error->description = qdr_field(desc);

        if (info_ok) {
            error->info = pn_data(0);
            pn_data_copy(error->info, info);
        }
    }

    return error;
}


qdr_error_t *qdr_error(const char *name, const char *description)
{
    qdr_error_t *error = new_qdr_error_t();

    error->name        = qdr_field(name);
    error->description = qdr_field(description);
    error->info        = 0;

    return error;
}


void qdr_error_free(qdr_error_t *error)
{
    if (error == 0)
        return;

    qdr_field_free(error->name);
    qdr_field_free(error->description);
    if (error->info)
        pn_data_free(error->info);

    free_qdr_error_t(error);
}


void qdr_error_copy(qdr_error_t *from, pn_condition_t *to)
{
    if (from == 0)
        return;

    if (from->name) {
        unsigned char *name = qd_iterator_copy(from->name->iterator);
        pn_condition_set_name(to, (char*) name);
        free(name);
    }

    if (from->description) {
        unsigned char *desc = qd_iterator_copy(from->description->iterator);
        pn_condition_set_description(to, (char*) desc);
        free(desc);
    }

    if (from->info)
        pn_data_copy(pn_condition_info(to), from->info);
}


char *qdr_error_description(const qdr_error_t *err)
{
    if (!err || !err->description || !err->description->iterator)
        return 0;
    int   length = qd_iterator_length(err->description->iterator);
    char *text   = (char*) malloc(length + 1);
    qd_iterator_ncopy(err->description->iterator, (unsigned char*) text, length);
    text[length] = '\0';
    return text;
}

char *qdr_error_name(const qdr_error_t *err)
{
    if (!err || !err->name || !err->name->iterator)
        return 0;
    int   length = qd_iterator_length(err->name->iterator);
    char *text   = (char*) malloc(length + 1);
    qd_iterator_ncopy(err->name->iterator, (unsigned char*) text, length);
    text[length] = '\0';
    return text;
}


pn_data_t *qdr_error_info(const qdr_error_t *err)
{
    if (err == 0)
        return 0;

    return err->info;
}

