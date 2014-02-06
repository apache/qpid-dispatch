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

#include <stdio.h>
#include <qpid/dispatch.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include "config.h"

static int            exit_with_sigint = 0;
static qd_dispatch_t *dispatch;

static const char *app_config =
    "from qpid_dispatch_internal.config.schema import config_schema\n"
    "config_schema['fixed-address'] = (False, {\n"
    "   'prefix' : (str, 0,    'M', None, None),\n"
    "   'fanout' : (str, None, '', 'multiple', ['multiple', 'single']),\n"
    "   'bias'   : (str, None, '', 'closest',  ['closest', 'spread'])})\n";


/**
 * The thread_start_handler is invoked once for each server thread at thread startup.
 */
static void thread_start_handler(void* context, int thread_id)
{
}


/**
 * This is the OS signal handler, invoked on an undetermined thread at a completely
 * arbitrary point of time.  It is not safe to do anything here but signal the dispatch
 * server with the signal number.
 */
static void signal_handler(int signum)
{
    qd_server_signal(dispatch, signum);
}


/**
 * This signal handler is called cleanly by one of the server's worker threads in
 * response to an earlier call to qd_server_signal.
 */
static void server_signal_handler(void* context, int signum)
{
    qd_server_pause(dispatch);

    switch (signum) {
    case SIGINT:
        exit_with_sigint = 1;

    case SIGQUIT:
    case SIGTERM:
        fflush(stdout);
        qd_server_stop(dispatch);
        break;

    case SIGHUP:
        break;

    default:
        break;
    }

    qd_server_resume(dispatch);
}


int main(int argc, char **argv)
{
#define DEFAULT_DISPATCH_PYTHON_DIR QPID_DISPATCH_HOME_INSTALLED "/python"
    const char *config_path   = DEFAULT_CONFIG_PATH;
    const char *python_pkgdir = DEFAULT_DISPATCH_PYTHON_DIR;

    static struct option long_options[] = {
    {"config",  required_argument, 0, 'c'},
    {"include", required_argument, 0, 'I'},
    {"help",    no_argument,       0, 'h'},
    {0,         0,                 0,  0}
    };

    while (1) {
        int c = getopt_long(argc, argv, "c:I:h", long_options, 0);
        if (c == -1)
            break;

        switch (c) {
        case 'c' :
            config_path = optarg;
            break;

        case 'I' :
            python_pkgdir = optarg;
            break;

        case 'h' :
            printf("Usage: %s [OPTION]\n\n", argv[0]);
            printf("  -c, --config=PATH (%s)\n", DEFAULT_CONFIG_PATH);
            printf("                             Load configuration from file at PATH\n");
            printf("  -I, --include=PATH (%s)\n", DEFAULT_DISPATCH_PYTHON_DIR);
            printf("                             Location of Dispatch's Python library\n");
            printf("  -h, --help                 Print this help\n");
            exit(0);

        case '?' :
            exit(1);
        }
    }

    qd_log_set_mask(0xFFFFFFFE);

    dispatch = qd_dispatch(python_pkgdir);
    qd_dispatch_extend_config_schema(dispatch, app_config);
    qd_dispatch_load_config(dispatch, config_path);
    qd_dispatch_configure_container(dispatch);
    qd_dispatch_configure_router(dispatch);
    qd_dispatch_prepare(dispatch);
    qd_dispatch_post_configure_connections(dispatch);

    qd_server_set_signal_handler(dispatch, server_signal_handler, 0);
    qd_server_set_start_handler(dispatch, thread_start_handler, 0);

    signal(SIGHUP,  signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);

    qd_server_run(dispatch);
    qd_dispatch_free(dispatch);

    if (exit_with_sigint) {
        signal(SIGINT, SIG_DFL);
        kill(getpid(), SIGINT);
    }

    return 0;
}

