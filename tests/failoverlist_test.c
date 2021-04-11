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

#define _GNU_SOURCE
#include "qpid/dispatch/failoverlist.h"

#include "test_case.h"

#include <stdio.h>
#include <string.h>

static char *test_failover_list_empty(void *unused)
{
    qd_failover_list_t *list = qd_failover_list("");
    if (list != 0) {
        qd_failover_list_free(list);
        return "Expected parse failure";
    }
    return 0;
}


static char *test_failover_list_single(void *unused)
{
    char *fail = 0;

    qd_failover_list_t *list = qd_failover_list("host1");
    if (qd_failover_list_size(list) != 1) {
        fail = "1:Expected list size of 1";
    } else if (qd_failover_list_scheme(list, 0) != 0) {
        fail = "1:Expected null scheme";
    } else if (strcmp(qd_failover_list_host(list, 0), "host1")) {
        fail = "1:Incorrect host value";
    } else if (strcmp(qd_failover_list_port(list, 0), "5672")) {
        fail = "1:Incorrect default port value";
    } else if (qd_failover_list_hostname(list, 0))
        fail = "1:Expected null hostname";
    qd_failover_list_free(list);

    if (fail)
        return fail;

    list = qd_failover_list("amqps://host2");
    if (qd_failover_list_size(list) != 1) {
        fail = "2:Expected list size of 1";
    } else if (strcmp(qd_failover_list_scheme(list, 0), "amqps")) {
        fail = "2:Expected amqps scheme";
    } else if (strcmp(qd_failover_list_host(list, 0), "host2")) {
        fail = "2:Incorrect host value";
    } else if (strcmp(qd_failover_list_port(list, 0), "5672")) {
        fail = "2:Incorrect default port value";
    } else if (qd_failover_list_hostname(list, 0))
        fail = "2:Expected null hostname";
    qd_failover_list_free(list);

    if (fail)
        return fail;

    list = qd_failover_list("host3:10000");
    if (qd_failover_list_size(list) != 1) {
        fail = "3:Expected list size of 1";
    } else if (qd_failover_list_scheme(list, 0) != 0) {
        fail = "3:Expected null scheme";
    } else if (strcmp(qd_failover_list_host(list, 0), "host3")) {
        fail = "3:Incorrect host value";
    } else if (strcmp(qd_failover_list_port(list, 0), "10000")) {
        fail = "3:Incorrect default port value";
    } else if (qd_failover_list_hostname(list, 0))
        fail = "3:Expected null hostname";
    qd_failover_list_free(list);

    if (fail)
        return fail;

    list = qd_failover_list("amqp://host4:15000");
    if (qd_failover_list_size(list) != 1) {
        fail = "4:Expected list size of 1";
    } else if (strcmp(qd_failover_list_scheme(list, 0), "amqp")) {
        fail = "4:Expected amqp scheme";
    } else if (strcmp(qd_failover_list_host(list, 0), "host4")) {
        fail = "4:Incorrect host value";
    } else if (strcmp(qd_failover_list_port(list, 0), "15000")) {
        fail = "4:Incorrect default port value";
    } else if (qd_failover_list_hostname(list, 0))
        fail = "4:Expected null hostname";
    qd_failover_list_free(list);

    return fail;
}


static char *test_failover_list_multiple(void *unused)
{
    char *fail = 0;
    qd_failover_list_t *list = qd_failover_list("host1,amqps://host2 , host3:10000, amqp://host4:15000");
    if (qd_failover_list_size(list) != 4) {
        fail = "1:Expected list size of 4";
    } else if (qd_failover_list_scheme(list, 0) != 0) {
        fail = "1:Expected null scheme";
    } else if (strcmp(qd_failover_list_host(list, 0), "host1")) {
        fail = "1:Incorrect host value";
    } else if (strcmp(qd_failover_list_port(list, 0), "5672")) {
        fail = "1:Incorrect default port value";
    } else if (qd_failover_list_hostname(list, 0)) {
        fail = "1:Expected null hostname";

    } else if (strcmp(qd_failover_list_scheme(list, 1), "amqps")) {
        fail = "2:Expected amqps scheme";
    } else if (strcmp(qd_failover_list_host(list, 1), "host2")) {
        fail = "2:Incorrect host value";
    } else if (strcmp(qd_failover_list_port(list, 1), "5672")) {
        fail = "2:Incorrect default port value";
    } else if (qd_failover_list_hostname(list, 1)) {
        fail = "2:Expected null hostname";

    } else if (qd_failover_list_scheme(list, 2) != 0) {
        fail = "3:Expected null scheme";
    } else if (strcmp(qd_failover_list_host(list, 2), "host3")) {
        fail = "3:Incorrect host value";
    } else if (strcmp(qd_failover_list_port(list, 2), "10000")) {
        fail = "3:Incorrect default port value";
    } else if (qd_failover_list_hostname(list, 2)) {
        fail = "3:Expected null hostname";

    } else if (strcmp(qd_failover_list_scheme(list, 3), "amqp")) {
        fail = "4:Expected amqp scheme";
    } else if (strcmp(qd_failover_list_host(list, 3), "host4")) {
        fail = "4:Incorrect host value";
    } else if (strcmp(qd_failover_list_port(list, 3), "15000")) {
        fail = "4:Incorrect default port value";
    } else if (qd_failover_list_hostname(list, 3))
        fail = "4:Expected null hostname";
    qd_failover_list_free(list);

    return fail;
}


int failoverlist_tests()
{
    int result = 0;
    char *test_group = "failover_tests";

    TEST_CASE(test_failover_list_empty, 0);
    TEST_CASE(test_failover_list_single, 0);
    TEST_CASE(test_failover_list_multiple, 0);

    return result;
}

