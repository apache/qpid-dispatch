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

#include "config.h"

#include "qpid/dispatch.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int            exit_with_sigint = 0;
static qd_dispatch_t *dispatch = 0;
static qd_log_source_t *log_source = 0;
static const char* argv0 = 0;


/**
 * This is the OS signal handler, invoked on an undetermined thread at a completely
 * arbitrary point of time.
 */
static void signal_handler(int signum)
{
    /* Ignore future signals, dispatch may already be freed */
    signal(SIGHUP,  SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT,  SIG_IGN);
    switch (signum) {
    case SIGINT:
        exit_with_sigint = 1;
        // fallthrough
    case SIGQUIT:
    case SIGTERM:
        qd_server_stop(dispatch); /* qpid_server_stop is signal-safe */
        break;
    default:
        break;
    }
}

static void check(int fd) {
    if (qd_error_code()) {
        qd_log(log_source, QD_LOG_CRITICAL, "Router start-up failed: %s", qd_error_message());
        #ifdef __sun
        FILE *file = fdopen(fd, "a+");
        fprintf(file, "%s: %s\n", argv0, qd_error_message());
        #else
        dprintf(fd, "%s: %s\n", argv0, qd_error_message());
        #endif
        close(fd);
        exit(1);
    }
}

#define fail(fd, ...)                                   \
    do {                                                \
        if (errno)                                      \
            qd_error_errno(errno, __VA_ARGS__);         \
        else                                            \
            qd_error(QD_ERROR_RUNTIME, __VA_ARGS__);    \
        check(fd);                                      \
    } while(false)

static void main_process(const char *config_path, const char *python_pkgdir, bool test_hooks, int fd)
{
    dispatch = qd_dispatch(python_pkgdir, test_hooks);
    check(fd);
    log_source = qd_log_source("MAIN"); /* Logging is initialized by qd_dispatch. */
    qd_dispatch_validate_config(config_path);
    check(fd);
    qd_dispatch_load_config(dispatch, config_path);
    check(fd);

    signal(SIGHUP,  signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);

    if (fd > 2) {               /* Daemon mode, fd is one end of a pipe not stdout or stderr */
        #ifdef __sun
        const char * okResult = "ok";
        write(fd, okResult, (strlen(okResult)+1));
        #else
        dprintf(fd, "ok"); // Success signal
        #endif
        close(fd);
    }

    qd_server_run(dispatch);

    qd_dispatch_t *d = dispatch;
    dispatch = NULL;
    qd_dispatch_free(d);

    fflush(stdout);
    if (exit_with_sigint) {
        signal(SIGINT, SIG_DFL);
        kill(getpid(), SIGINT);
    }
}


static void daemon_process(const char *config_path, const char *python_pkgdir, bool test_hooks,
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
        if (setsid() < 0) fail(pipefd[1], "Cannot start a new session");
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
            if (fd != 0) fail(pipefd[1], "Can't redirect stdin to /dev/null");
            if (dup(fd) < 0) fail(pipefd[1], "Can't redirect stdout to /dev/null");
            if (dup(fd) < 0) fail(pipefd[1], "Can't redirect stderr /dev/null");

            //
            // Set the umask to 0
            //
            umask(0);


            //
            // If config path is not a fully qualified path, then construct the
            // fully qualified path to the config file.  This needs to be done
            // since the daemon will set "/" to its working directory.
            //
            char *config_path_full = NULL;
            if (strncmp("/", config_path, 1)) {
                size_t path_size = PATH_MAX;
                char *cur_path = (char *) calloc(path_size, sizeof(char));
                errno = 0;

                while (getcwd(cur_path, path_size) == NULL) {
                    free(cur_path);
                    if (errno != ERANGE) {
                        // Hard failure - can't recover from this
                        perror("Unable to determine current directory");
                        exit(1);
                    }
                    // errno == ERANGE: the current path does not fit, allocate
                    // more memory
                    path_size += 256;
                    cur_path = (char *) calloc(path_size, sizeof(char));
                    errno = 0;
                }

                // Populating fully qualified config file name
                const char *path_sep = !strcmp("/", cur_path) ? "" : "/";
                size_t cpf_len = strlen(cur_path) + strlen(path_sep) + strlen(config_path) + 1;
                config_path_full = calloc(cpf_len, sizeof(char));
                snprintf(config_path_full, cpf_len, "%s%s%s",
                         cur_path, path_sep, config_path);

                // Releasing temporary path variable
                memset(cur_path, 0, path_size * sizeof(char));
                free(cur_path);
            }

            //
            // Set the current directory to "/" to avoid blocking
            // mount points
            //
            if (chdir("/") < 0) fail(pipefd[1], "Can't chdir /");

            //
            // If a pidfile was provided, write the daemon pid there.
            //
            if (pidfile) {
                FILE *pf = fopen(pidfile, "w");
                if (pf == 0) fail(pipefd[1], "Can't write pidfile %s", pidfile);
                fprintf(pf, "%d\n", getpid());
                fclose(pf);
            }

            //
            // If a user was provided, drop privileges to the user's
            // privilege level.
            //
            if (user) {
                struct passwd *pwd = getpwnam(user);
                if (pwd == 0) fail(pipefd[1], "Can't look up user %s", user);
                if (setuid(pwd->pw_uid) < 0) fail(pipefd[1], "Can't set user ID for user %s, errno=%d", user, errno);
                //if (setgid(pwd->pw_gid) < 0) fail(pipefd[1], "Can't set group ID for user %s, errno=%d", user, errno);
            }

            main_process((config_path_full ? config_path_full : config_path), python_pkgdir, test_hooks, pipefd[1]);

            free(config_path_full);
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
        close(pipefd[1]); // Close write end.
        char result[256];
        memset(result, 0, sizeof(result));
        if (read(pipefd[0], &result, sizeof(result)-1) < 0) {
            perror("Error reading inter-process pipe");
            exit(1);
        }

        if (strcmp(result, "ok") == 0)
            exit(0);
        fprintf(stderr, "%s", result);
        exit(1);
    }
}

#define DEFAULT_DISPATCH_PYTHON_DIR QPID_DISPATCH_HOME_INSTALLED "/python"

void usage(char **argv) {
    fprintf(stdout, "Usage: %s [OPTIONS]\n\n", argv[0]);
    fprintf(stdout, "  -c, --config=PATH (%s)\n", DEFAULT_CONFIG_PATH);
    fprintf(stdout, "                             Load configuration from file at PATH\n");
    fprintf(stdout, "  -I, --include=PATH (%s)\n", DEFAULT_DISPATCH_PYTHON_DIR);
    fprintf(stdout, "                             Location of Dispatch's Python library\n");
    fprintf(stdout, "  -d, --daemon               Run process as a SysV-style daemon\n");
    fprintf(stdout, "  -P, --pidfile              If daemon, the file for the stored daemon pid\n");
    fprintf(stdout, "  -U, --user                 If daemon, the username to run as\n");
    fprintf(stdout, "  -T, --test-hooks           Enable internal system testing features\n");
    fprintf(stdout, "  -v, --version              Print the version of Qpid Dispatch Router\n");
    fprintf(stdout, "  -h, --help                 Print this help\n");
}

int main(int argc, char **argv)
{
    argv0 = argv[0];
    const char *config_path   = DEFAULT_CONFIG_PATH;
    const char *python_pkgdir = DEFAULT_DISPATCH_PYTHON_DIR;
    const char *pidfile = 0;
    const char *user    = 0;
    bool        daemon_mode = false;
    bool        test_hooks  = false;

    static struct option long_options[] = {
    {"config",  required_argument, 0, 'c'},
    {"include", required_argument, 0, 'I'},
    {"daemon",  no_argument,       0, 'd'},
    {"pidfile", required_argument, 0, 'P'},
    {"user",    required_argument, 0, 'U'},
    {"help",    no_argument,       0, 'h'},
    {"version", no_argument,       0, 'v'},
    {"test-hooks", no_argument,    0, 'T'},
    {0,         0,                 0,  0}
    };

    while (1) {
        int c = getopt_long(argc, argv, "c:I:dP:U:h:vT", long_options, 0);
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
            usage(argv);
            exit(0);

        case 'v' :
            fprintf(stdout, "%s\n", QPID_DISPATCH_VERSION);
            exit(0);

        case 'T' :
            test_hooks = true;
            break;

        case '?' :
            usage(argv);
            exit(1);
        }
    }
    if (optind < argc) {
        fprintf(stderr, "Unexpected arguments:");
        for (; optind < argc; ++optind) fprintf(stderr, " %s", argv[optind]);
        fprintf(stderr, "\n\n");
        usage(argv);
        exit(1);
    }

    if (daemon_mode)
        daemon_process(config_path, python_pkgdir, test_hooks, pidfile, user);
    else
        main_process(config_path, python_pkgdir, test_hooks, 2);

    return 0;
}
