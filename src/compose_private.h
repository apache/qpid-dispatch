#ifndef __compose_private_h__
#define __compose_private_h__ 1
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

#include "message_private.h"

#include "qpid/dispatch/compose.h"

qd_buffer_list_t *qd_compose_buffers(qd_composed_field_t *field);

typedef struct qd_composite_t {
    DEQ_LINKS(struct qd_composite_t);
    int                 isMap;
    uint32_t            count;
    uint32_t            length;
    qd_field_location_t length_location;
    qd_field_location_t count_location;
} qd_composite_t;

ALLOC_DECLARE(qd_composite_t);
DEQ_DECLARE(qd_composite_t, qd_field_stack_t);


struct qd_composed_field_t {
    qd_buffer_list_t buffers;
    qd_field_stack_t fieldStack;
};

ALLOC_DECLARE(qd_composed_field_t);

#endif
