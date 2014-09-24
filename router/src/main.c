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
#include <sys/stat.h>
#include <pwd.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include "config.h"

static int            exit_with_sigint = 0;
static qd_dispatch_t *dispatch = 0;
static qd_log_source_t *log_source = 0;

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

static void check(int fd) {
    if (qd_error_code()) {
	qd_log(log_source, QD_LOG_CRITICAL, "Router start-up failed: %s", qd_error_message());
    if (fd > 0)
        write(fd, "X", 1);
	exit(1);
    }
}


static void main_process(const char *config_path, const char *python_pkgdir, int fd)
{
    qd_error_clear();
    dispatch = qd_dispatch(python_pkgdir);
    check(fd);
    log_source = qd_log_source("MAIN"); /* Logging is initialized by qd_dispatch. */
    qd_dispatch_load_config(dispatch, config_path);
    check(fd);

    (void)server_signal_handler; (void)thread_start_handler;(void)signal_handler;
    qd_server_set_signal_handler(dispatch, server_signal_handler, 0);
    qd_server_set_start_handler(dispatch, thread_start_handler, 0);

    signal(SIGHUP,  signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);

    if (fd > 0) {
        write(fd, "0", 1); // Success signal
        close(fd);
    }

    qd_server_run(dispatch);
    qd_dispatch_free(dispatch);

    if (exit_with_sigint) {
        signal(SIGINT, SIG_DFL);
        kill(getpid(), SIGINT);
    }
}


static void daemon_process(const char *config_path, const char *python_pkgdir,
                           const char *pidfile, const char *user)
{
    int pipefd[2];

    //
    // This daemonization process is based on that outlined in the
    // "daemon" manpage from Linux.
    //

    //
    // Create an unnamed pipe for communication from the daemon to the main process
    //
    if (pipe(pipefd) < 0) {
        perror("Error creating inter-process pipe");
        exit(1);
    }

    //
    // First fork
    //
    pid_t pid = fork();
    if (pid == 0) {
        //
        // Child Process
        //

        //
        // Detach any terminals and create an independent session
        //
        if (setsid() < 0) {
            write(pipefd[1], "1", 1);
            exit(0);
        }

        //
        // Second fork
        //
        pid_t pid2 = fork();
        if (pid2 == 0) {
            close(pipefd[0]); // Close read end.

            //
            // Assign stdin, stdout, and stderr to /dev/null
            //
            close(2);
            close(1);
            close(0);
            int fd = open("/dev/null", O_RDWR);
            if (fd != 0) {
                write(pipefd[1], "2", 1);
                exit(0);
            }
            if (dup(fd) < 0) {
                write(pipefd[1], "3", 1);
                exit(0);
            }
            if (dup(fd) < 0) {
                write(pipefd[1], "4", 1);
                exit(0);
            }

            //
            // Set the umask to 0
            //
            if (umask(0) < 0) {
                write(pipefd[1], "5", 1);
                exit(0);
            }

            //
            // Set the current directory to "/" to avoid blocking
            // mount points
            //
            if (chdir("/") < 0) {
                write(pipefd[1], "6", 1);
                exit(0);
            }

            //
            // If a pidfile was provided, write the daemon pid there.
            //
            if (pidfile) {
                FILE *pf = fopen(pidfile, "w");
                if (pf == 0) {
                    write(pipefd[1], "7", 1);
                    exit(0);
                }
                fprintf(pf, "%d\n", getpid());
                fclose(pf);
            }

            //
            // If a user was provided, drop privileges to the user's
            // privilege level.
            //
            if (user) {
                struct passwd *pwd = getpwnam(user);
                if (pwd == 0) {
                    write(pipefd[1], "8", 1);
                    exit(0);
                }
                if (setuid(pwd->pw_uid) < 0) {
                    write(pipefd[1], "9", 1);
                    exit(0);
                }
                if (setgid(pwd->pw_gid) < 0) {
                    write(pipefd[1], "A", 1);
                    exit(0);
                }
            }

            main_process(config_path, python_pkgdir, pipefd[1]);
        } else
            //
            // Exit first child
            //
            exit(0);
    } else {
        //
        // Parent Process
        // Wait for a success signal ('0') from the daemon process.
        // If we get success, exit with 0.  Otherwise, exit with 1.
        //
        char code;
        close(pipefd[1]); // Close write end.
        if (read(pipefd[0], &code, 1) < 0) {
            perror("Error reading inter-process pipe");
            exit(1);
        }

        if (code == '0')
            exit(0);
        fprintf(stderr, "Error occurred during daemon initialization, please see logs.  [code=%c]\n", code);
        exit(1);
    }
}


int main(int argc, char **argv)
{
#define DEFAULT_DISPATCH_PYTHON_DIR QPID_DISPATCH_HOME_INSTALLED "/python"
    const char *config_path   = DEFAULT_CONFIG_PATH;
    const char *python_pkgdir = DEFAULT_DISPATCH_PYTHON_DIR;
    const char *pidfile = 0;
    const char *user    = 0;
    bool        daemon_mode = false;

    static struct option long_options[] = {
    {"config",  required_argument, 0, 'c'},
    {"include", required_argument, 0, 'I'},
    {"daemon",  no_argument,       0, 'd'},
    {"pidfile", required_argument, 0, 'P'},
    {"user",    required_argument, 0, 'U'},
    {"help",    no_argument,       0, 'h'},
    {0,         0,                 0,  0}
    };

    while (1) {
        int c = getopt_long(argc, argv, "c:I:dP:U:h", long_options, 0);
        if (c == -1)
            break;

        switch (c) {
        case 'c' :
            config_path = optarg;
            break;

        case 'I' :
            python_pkgdir = optarg;
            break;

        case 'd' :
            daemon_mode = true;
            break;

        case 'P' :
            pidfile = optarg;
            break;

        case 'U' :
            user = optarg;
            break;

        case 'h' :
            printf("Usage: %s [OPTIONS]\n\n", argv[0]);
            printf("  -c, --config=PATH (%s)\n", DEFAULT_CONFIG_PATH);
            printf("                             Load configuration from file at PATH\n");
            printf("  -I, --include=PATH (%s)\n", DEFAULT_DISPATCH_PYTHON_DIR);
            printf("                             Location of Dispatch's Python library\n");
            printf("  -d, --daemon               Run process as a SysV-style daemon\n");
            printf("  -P, --pidfile              If daemon, the file for the stored daemon pid\n");
            printf("  -U, --user                 If daemon, the username to run as\n");
            printf("  -h, --help                 Print this help\n");
            exit(0);

        case '?' :
            exit(1);
        }
    }

    if (daemon_mode)
        daemon_process(config_path, python_pkgdir, pidfile, user);
    else
        main_process(config_path, python_pkgdir, -1);

    return 0;
}


