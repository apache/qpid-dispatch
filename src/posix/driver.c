/*
 *
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
 *
 */

#include <assert.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>

#ifndef __sun
#include <sys/eventfd.h>
#endif

#ifdef __sun
#include <signal.h>
#endif

#include <qpid/dispatch/driver.h>
#include <qpid/dispatch/error.h>
#include <qpid/dispatch/threading.h>
#include <proton/error.h>
#include <proton/ssl.h>
#include <proton/object.h>
#include <qpid/dispatch/ctools.h>
#include "alloc.h"
#include "aprintf.h"
#include "log_private.h"

/* Decls */

#define MAX_HOST  1024
#define MAX_SERV  256
#define ERROR_MAX 128

#define PN_SEL_RD (0x0001)
#define PN_SEL_WR (0x0002)

DEQ_DECLARE(qdpn_listener_t, qdpn_listener_list_t);
DEQ_DECLARE(qdpn_connector_t, qdpn_connector_list_t);

const char *protocol_family_ipv4 = "IPv4";
const char *protocol_family_ipv6 = "IPv6";

const char *AF_INET6_STR = "AF_INET6";
const char *AF_INET_STR = "AF_INET";

static inline void ignore_result(int unused_result) {
    (void) unused_result;
}

struct qdpn_driver_t {
    qd_log_source_t *log;
    sys_mutex_t     *lock;

    //
    // The following values need to be protected by lock from multi-threaded access.
    //
    qdpn_listener_list_t   listeners;
    qdpn_connector_list_t  connectors;
    qdpn_listener_t       *listener_next;
    qdpn_connector_t      *connector_next;
    size_t                 closed_count;

    //
    // The following values will only be accessed by one thread at a time.
    //
    size_t          capacity;
    struct pollfd  *fds;
    size_t          nfds;
#ifdef __sun
    int             ctrl[2];
#else
    int             efd;    // Event-FD for signaling the poll (driver-wakeup)
#endif
    pn_timestamp_t  wakeup;
};

struct qdpn_listener_t {
    DEQ_LINKS(qdpn_listener_t);
    qdpn_driver_t *driver;
    void *context;
    int idx;
    int fd;
    bool pending:1;
    bool closed:1;
};

#define PN_NAME_MAX (256)

struct qdpn_connector_t {
    DEQ_LINKS(qdpn_connector_t);
    qdpn_driver_t *driver;
    char name[PN_NAME_MAX];
    char hostip[PN_NAME_MAX];
    pn_timestamp_t wakeup;
    pn_connection_t *connection;
    pn_transport_t *transport;
    qdpn_listener_t *listener;
    void *context;
    qdpn_connector_methods_t *methods;
    int idx;
    int fd;
    int status;
    bool pending_tick:1;
    bool pending_read:1;
    bool pending_write:1;
    bool socket_error:1;
    bool hangup:1;
    bool closed:1;
};

ALLOC_DECLARE(qdpn_listener_t);
ALLOC_DEFINE(qdpn_listener_t);

ALLOC_DECLARE(qdpn_connector_t);
ALLOC_DEFINE(qdpn_connector_t);

/* Impls */

static void qdpn_log_errno(qdpn_driver_t *d, const char *fmt, ...)
{
    char msg[QD_LOG_TEXT_MAX];
    char *begin = msg, *end = msg+sizeof(msg);
    va_list ap;
    va_start(ap, fmt);
    vaprintf(&begin, end, fmt, ap);
    va_end(ap);
    aprintf(&begin, end, ": ");
    strerror_r(errno, begin, end - begin);
    qd_log(d->log, QD_LOG_ERROR, "%s", msg);
}


pn_timestamp_t pn_i_now(void)
{
    struct timespec now;
#ifdef CLOCK_MONOTONIC_COARSE
    int cid = CLOCK_MONOTONIC_COARSE;
#else
    int cid = CLOCK_MONOTONIC;
#endif
    if (clock_gettime(cid, &now)) {
        qd_error_errno(errno, "clock_gettime");
        exit(1);
    }
    return ((pn_timestamp_t)now.tv_sec) * 1000 + (now.tv_nsec / 1000000);
}

pn_timestamp_t qdpn_now() { return pn_i_now(); }

#define pn_min(X,Y) ((X) > (Y) ? (Y) : (X))
#define pn_max(X,Y) ((X) < (Y) ? (Y) : (X))

static pn_timestamp_t pn_timestamp_min( pn_timestamp_t a, pn_timestamp_t b )
{
    if (a && b) return pn_min(a, b);
    if (a) return a;
    return b;
}

// listener

static void qdpn_driver_add_listener(qdpn_driver_t *d, qdpn_listener_t *l)
{
    if (!l->driver) return;
    sys_mutex_lock(d->lock);
    DEQ_INSERT_TAIL(d->listeners, l);
    sys_mutex_unlock(d->lock);
    l->driver = d;
}

static void qdpn_driver_remove_listener(qdpn_driver_t *d, qdpn_listener_t *l)
{
    if (!l->driver) return;

    sys_mutex_lock(d->lock);
    if (l == d->listener_next)
        d->listener_next = DEQ_NEXT(l);
    DEQ_REMOVE(d->listeners, l);
    sys_mutex_unlock(d->lock);

    l->driver = NULL;
}


static int qdpn_create_socket(int af)
{
    struct protoent *pe_tcp = getprotobyname("tcp");
    if (pe_tcp == NULL)
        return -1;
    return socket(af, SOCK_STREAM, pe_tcp->p_proto);
}


static void qdpn_configure_sock(qdpn_driver_t *driver, int sock, bool tcp)
{
    //
    // Set the socket to be non-blocking for asynchronous operation.
    //
    int flags = fcntl(sock, F_GETFL);
    flags |= O_NONBLOCK;
    if (fcntl(sock, F_SETFL, flags) < 0)
        qdpn_log_errno(driver, "fcntl");

    //
    // Disable the Nagle algorithm on TCP connections.
    //
    // Note:  It would be more correct for the "level" argument to be SOL_TCP.  However, there
    //        are portability issues with this macro so we use IPPROTO_TCP instead.
    //
    if (tcp) {
        int tcp_nodelay = 1;
        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void*) &tcp_nodelay, sizeof(tcp_nodelay)) < 0)
            qdpn_log_errno(driver, "setsockopt");
    }
}


/**
 * Sets the ai_family field on the addrinfo struct based on the passed in NON-NULL protocol_family.
 * If the passed in protocol family does not match IPv6, IPv4, the function does not set the ai_family field
 */
static void qd_set_addr_ai_family(qdpn_driver_t *driver, struct addrinfo *addr, const char* protocol_family)
{
    if (protocol_family) {
        if(strcmp(protocol_family, protocol_family_ipv6) == 0)
            addr->ai_family = AF_INET6;
        else if(strcmp(protocol_family, protocol_family_ipv4) == 0)
            addr->ai_family = AF_INET;
    }
}


qdpn_listener_t *qdpn_listener(qdpn_driver_t *driver,
                               const char *host,
                               const char *port,
                               const char *protocol_family,
                               void* context)
{
    if (!driver) return NULL;

    struct addrinfo hints = {0}, *addr;
    hints.ai_socktype = SOCK_STREAM;
    int code = getaddrinfo(host, port, &hints, &addr);
    if (code) {
        qd_log(driver->log, QD_LOG_ERROR, "getaddrinfo(%s, %s): %s", host, port, gai_strerror(code));
        return 0;
    }

    // Set the protocol family before creating the socket.
    qd_set_addr_ai_family(driver, addr, protocol_family);

    int sock = qdpn_create_socket(addr->ai_family);
    if (sock < 0) {
        qdpn_log_errno(driver, "pn_create_socket");
        freeaddrinfo(addr);
        return 0;
    }

    int optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        qdpn_log_errno(driver, "setsockopt");
        close(sock);
        freeaddrinfo(addr);
        return 0;
    }

    if (bind(sock, addr->ai_addr, addr->ai_addrlen) == -1) {
        qdpn_log_errno(driver, "bind");
        freeaddrinfo(addr);
        close(sock);
        return 0;
    }

    freeaddrinfo(addr);

    if (listen(sock, 50) == -1) {
        qdpn_log_errno(driver, "listen");
        close(sock);
        return 0;
    }

    qdpn_listener_t *l = qdpn_listener_fd(driver, sock, context);
    return l;
}

qdpn_listener_t *qdpn_listener_fd(qdpn_driver_t *driver, int fd, void *context)
{
    if (!driver) return NULL;

    qdpn_listener_t *l = new_qdpn_listener_t();
    if (!l) return NULL;
    DEQ_ITEM_INIT(l);
    l->driver = driver;
    l->idx = 0;
    l->pending = false;
    l->fd = fd;
    l->closed = false;
    l->context = context;

    qdpn_driver_add_listener(driver, l);
    return l;
}

int qdpn_listener_get_fd(qdpn_listener_t *listener)
{
    assert(listener);
    return listener->fd;
}

qdpn_listener_t *qdpn_listener_head(qdpn_driver_t *driver)
{
    if (!driver)
        return 0;

    qdpn_listener_t *head;
    sys_mutex_lock(driver->lock);
    head = DEQ_HEAD(driver->listeners);
    sys_mutex_unlock(driver->lock);
    return head;
}

qdpn_listener_t *qdpn_listener_next(qdpn_listener_t *listener)
{
    if (!listener || !listener->driver)
        return 0;

    qdpn_listener_t *next;
    sys_mutex_lock(listener->driver->lock);
    next = DEQ_NEXT(listener);
    sys_mutex_unlock(listener->driver->lock);
    return next;
}

void *qdpn_listener_context(qdpn_listener_t *l)
{
    return l ? l->context : NULL;
}

void qdpn_listener_set_context(qdpn_listener_t *listener, void *context)
{
    assert(listener);
    listener->context = context;
}

qdpn_connector_t *qdpn_listener_accept(qdpn_listener_t *l,
                                       void *policy,
                                       bool (*policy_fn)(void *, const char *name),
                                       bool *counted)
{
    if (!l || !l->pending) return NULL;
    char name[PN_NAME_MAX];
    char serv[MAX_SERV];
    char hostip[MAX_HOST];

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_UNSPEC;
    socklen_t addrlen = sizeof(addr);

    int sock = accept(l->fd, (struct sockaddr *) &addr, &addrlen);
    if (sock < 0) {
        qdpn_log_errno(l->driver, "accept");
        return 0;
    } else {
        int code = getnameinfo((struct sockaddr *) &addr, addrlen, hostip, MAX_HOST, serv, MAX_SERV, NI_NUMERICHOST | NI_NUMERICSERV);
        if (code != 0) {
            qd_log(l->driver->log, QD_LOG_ERROR, "getnameinfo: %s", gai_strerror(code));
            close(sock);
            return 0;
        } else {
            qdpn_configure_sock(l->driver, sock, true);
            snprintf(name, PN_NAME_MAX-1, "%s:%s", hostip, serv);
        }
    }

    if (policy_fn) {
        if (!(*policy_fn)(policy, name)) {
            close(sock);
            return 0;
        } else {
            *counted = true;
        }
    }
    qdpn_connector_t *c = qdpn_connector_fd(l->driver, sock, NULL);
    snprintf(c->name, PN_NAME_MAX, "%s", name);
    snprintf(c->hostip, PN_NAME_MAX, "%s", hostip);
    c->listener = l;
    return c;
}

void qdpn_listener_close(qdpn_listener_t *l)
{
    if (!l) return;
    if (l->closed) return;

    if (close(l->fd) == -1)
        qdpn_log_errno(l->driver, "close");
    l->closed = true;
}

void qdpn_listener_free(qdpn_listener_t *l)
{
    if (!l) return;
    if (l->driver) qdpn_driver_remove_listener(l->driver, l);
    free_qdpn_listener_t(l);
}

// connector

static void qdpn_driver_add_connector(qdpn_driver_t *d, qdpn_connector_t *c)
{
    if (!c->driver) return;
    sys_mutex_lock(d->lock);
    DEQ_INSERT_TAIL(d->connectors, c);
    sys_mutex_unlock(d->lock);
    c->driver = d;
}

static void qdpn_driver_remove_connector(qdpn_driver_t *d, qdpn_connector_t *c)
{
    if (!c->driver) return;

    sys_mutex_lock(d->lock);
    if (c == d->connector_next) {
        d->connector_next = DEQ_NEXT(c);
    }

    DEQ_REMOVE(d->connectors, c);
    c->driver = NULL;
    if (c->closed) {
        d->closed_count--;
    }
    sys_mutex_unlock(d->lock);
}

qdpn_connector_t *qdpn_connector(qdpn_driver_t *driver,
                                 const char *host,
                                 const char *port,
                                 const char *protocol_family,
                                 void *context)
{
    if (!driver) return NULL;

    struct addrinfo hints = {0}, *addr;
    hints.ai_socktype = SOCK_STREAM;
    int code = getaddrinfo(host, port, &hints, &addr);
    if (code) {
        qd_log(driver->log, QD_LOG_ERROR, "getaddrinfo(%s, %s): %s", host, port, gai_strerror(code));
        return 0;
    }

    // Set the protocol family before creating the socket.
    qd_set_addr_ai_family(driver, addr, protocol_family);

    int sock = qdpn_create_socket(addr->ai_family);
    if (sock == PN_INVALID_SOCKET) {
        freeaddrinfo(addr);
        qdpn_log_errno(driver, "pn_create_socket");
        return 0;
    }

    qdpn_configure_sock(driver, sock, true);

    if (connect(sock, addr->ai_addr, addr->ai_addrlen) == -1) {
        if (errno != EINPROGRESS) {
            qdpn_log_errno(driver, "connect");
            freeaddrinfo(addr);
            close(sock);
            return 0;
        }
    }

    freeaddrinfo(addr);

    qdpn_connector_t *c = qdpn_connector_fd(driver, sock, context);
    snprintf(c->name, PN_NAME_MAX, "%s:%s", host, port);
    return c;
}


static void connector_process(qdpn_connector_t *c);
static void connector_close(qdpn_connector_t *c);

static qdpn_connector_methods_t connector_methods = {
    connector_process,
    connector_close
};

qdpn_connector_t *qdpn_connector_fd(qdpn_driver_t *driver, int fd, void *context)
{
    if (!driver) return NULL;

    qdpn_connector_t *c = new_qdpn_connector_t();
    if (!c) return NULL;
    DEQ_ITEM_INIT(c);
    c->driver = driver;
    c->pending_tick = false;
    c->pending_read = false;
    c->pending_write = false;
    c->socket_error = false;
    c->hangup = false;
    c->name[0] = '\0';
    c->idx = 0;
    c->fd = fd;
    c->status = PN_SEL_RD | PN_SEL_WR;
    c->closed = false;
    c->wakeup = 0;
    c->connection = NULL;
    c->transport = pn_transport();
    c->context = context;
    c->listener = NULL;
    c->methods = &connector_methods;
    qdpn_driver_add_connector(driver, c);
    return c;
}

int qdpn_connector_get_fd(qdpn_connector_t *connector)
{
    assert(connector);
    return connector->fd;
}

qdpn_connector_t *qdpn_connector_head(qdpn_driver_t *driver)
{
    if (!driver)
        return 0;

    sys_mutex_lock(driver->lock);
    qdpn_connector_t *head = DEQ_HEAD(driver->connectors);
    sys_mutex_unlock(driver->lock);
    return head;
}

qdpn_connector_t *qdpn_connector_next(qdpn_connector_t *connector)
{
    if (!connector || !connector->driver)
        return 0;
    sys_mutex_lock(connector->driver->lock);
    qdpn_connector_t *next = DEQ_NEXT(connector);
    sys_mutex_unlock(connector->driver->lock);
    return next;
}

pn_transport_t *qdpn_connector_transport(qdpn_connector_t *ctor)
{
    return ctor ? ctor->transport : NULL;
}

void qdpn_connector_set_connection(qdpn_connector_t *ctor, pn_connection_t *connection)
{
    if (!ctor) return;
    if (ctor->connection) {
        pn_class_decref(PN_OBJECT, ctor->connection);
        pn_transport_unbind(ctor->transport);
    }
    ctor->connection = connection;
    if (ctor->connection) {
        pn_class_incref(PN_OBJECT, ctor->connection);
        pn_transport_bind(ctor->transport, connection);
    }
}

pn_connection_t *qdpn_connector_connection(qdpn_connector_t *ctor)
{
    return ctor ? ctor->connection : NULL;
}

void *qdpn_connector_context(qdpn_connector_t *ctor)
{
    return ctor ? ctor->context : NULL;
}

void qdpn_connector_set_context(qdpn_connector_t *ctor, void *context)
{
    if (!ctor) return;
    ctor->context = context;
}

const char *qdpn_connector_name(const qdpn_connector_t *ctor)
{
    if (!ctor) return 0;
    return ctor->name;
}

const char *qdpn_connector_hostip(const qdpn_connector_t *ctor)
{
    if (!ctor) return 0;
    return ctor->hostip;
}

qdpn_listener_t *qdpn_connector_listener(qdpn_connector_t *ctor)
{
    return ctor ? ctor->listener : NULL;
}

/* Mark the connector as closed, but don't close the FD (already closed or
 * will be closed elsewhere)
 */
void qdpn_connector_mark_closed(qdpn_connector_t *ctor)
{
    if (!ctor || !ctor->driver) return;
    sys_mutex_lock(ctor->driver->lock);
    ctor->status = 0;
    if (!ctor->closed) {
        qd_log(ctor->driver->log, QD_LOG_TRACE, "closed %s", ctor->name);
        ctor->closed = true;
        ctor->driver->closed_count++;
    }
    sys_mutex_unlock(ctor->driver->lock);
}

static void connector_close(qdpn_connector_t *ctor)
{
    if (ctor && !ctor->closed) {
        qdpn_connector_mark_closed(ctor);
        if (close(ctor->fd) == -1)
            qdpn_log_errno(ctor->driver, "close %s", ctor->name);
    }
}

void qdpn_connector_close(qdpn_connector_t *c)
{
    if (c && !c->closed) c->methods->close(c);
}

bool qdpn_connector_closed(qdpn_connector_t *ctor)
{
    return ctor ? ctor->closed : true;
}

bool qdpn_connector_failed(qdpn_connector_t *ctor)
{
    return ctor ? ctor->socket_error : true;
}

void qdpn_connector_free(qdpn_connector_t *ctor)
{
    if (!ctor) return;

    if (ctor->driver) qdpn_driver_remove_connector(ctor->driver, ctor);
    pn_transport_unbind(ctor->transport);
    pn_transport_free(ctor->transport);
    ctor->transport = NULL;
    if (ctor->connection) pn_class_decref(PN_OBJECT, ctor->connection);
    ctor->connection = NULL;
    free_qdpn_connector_t(ctor);
}

void qdpn_connector_activate(qdpn_connector_t *ctor, qdpn_activate_criteria_t crit)
{
    switch (crit) {
    case QDPN_CONNECTOR_WRITABLE :
        ctor->status |= PN_SEL_WR;
        break;

    case QDPN_CONNECTOR_READABLE :
        ctor->status |= PN_SEL_RD;
        break;
    }
}


void qdpn_activate_all(qdpn_driver_t *d)
{
    sys_mutex_lock(d->lock);
    qdpn_connector_t *c = DEQ_HEAD(d->connectors);
    while (c) {
        c->status |= PN_SEL_WR;
        c = DEQ_NEXT(c);
    }
    sys_mutex_unlock(d->lock);
}

bool qdpn_connector_hangup(qdpn_connector_t *ctor) {
    return ctor->hangup;
}

bool qdpn_connector_activated(qdpn_connector_t *ctor, qdpn_activate_criteria_t crit)
{
    bool result = false;

    switch (crit) {
    case QDPN_CONNECTOR_WRITABLE :
        result = ctor->pending_write;
        ctor->pending_write = false;
        ctor->status &= ~PN_SEL_WR;
        break;

    case QDPN_CONNECTOR_READABLE :
        result = ctor->pending_read;
        ctor->pending_read = false;
        ctor->status &= ~PN_SEL_RD;
        break;
    }

    return result;
}

static pn_timestamp_t qdpn_connector_tick(qdpn_connector_t *ctor, pn_timestamp_t now)
{
    if (!ctor->transport) return 0;
    return pn_transport_tick(ctor->transport, now);
}

void qdpn_connector_process(qdpn_connector_t *c)
{
    if (c && !c->closed) c->methods->process(c);
}

static void connector_process(qdpn_connector_t *c)
{
    if(c->closed) return;

    pn_transport_t *transport = c->transport;
    c->status = 0;

    ///
    /// Socket read
    ///
    ssize_t capacity = pn_transport_capacity(transport);
    if (capacity > 0) {
        c->status |= PN_SEL_RD;
        if (c->pending_read) {
            c->pending_read = false;
            ssize_t n =  recv(c->fd, pn_transport_tail(transport), capacity, 0);
            if (n < 0) {
                if (errno != EAGAIN) {
                    qdpn_log_errno(c->driver, "recv %s", c->name);
                    pn_transport_close_tail( transport );
                }
            } else if (n == 0) { /* HUP */
                pn_transport_close_tail( transport );
            } else {
                pn_transport_process(transport, (size_t) n);
            }
        }
    }

    ///
    /// Event wakeup
    ///
    c->wakeup = qdpn_connector_tick(c, pn_i_now());

    ///
    /// Socket write
    ///
    ssize_t pending = pn_transport_pending(transport);
    if (pending > 0) {
        c->status |= PN_SEL_WR;
        if (c->pending_write) {
            c->pending_write = false;
#ifdef MSG_NOSIGNAL
            ssize_t n = send(c->fd, pn_transport_head(transport), pending, MSG_NOSIGNAL);
#else
            ssize_t n = send(c->fd, pn_transport_head(transport), pending, 0);
#endif
            if (n < 0) {
                if (errno != EAGAIN) {
                    qdpn_log_errno(c->driver, "send %s", c->name);
                    pn_transport_close_head( transport );
                }
            } else if (n) {
                pn_transport_pop(transport, (size_t) n);
            }
        }
    }

    if (pn_transport_closed(c->transport)) {
        qdpn_connector_close(c);
    }
}

// driver

qdpn_driver_t *qdpn_driver(qd_log_source_t *log)
{
    qdpn_driver_t *d = (qdpn_driver_t *) malloc(sizeof(qdpn_driver_t));
    if (!d) return NULL;
    ZERO(d);
    DEQ_INIT(d->listeners);
    DEQ_INIT(d->connectors);
    d->log = log;
    d->lock = sys_mutex();

#ifdef __sun
    if (pipe(d->ctrl))
        perror("Can't create control pipe");

    qdpn_configure_sock(d, d->ctrl[0], false);
    qdpn_configure_sock(d, d->ctrl[1], false);

    struct sigaction act;
    act.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &act, NULL);
#else
    d->efd = eventfd(0, EFD_NONBLOCK);
    if (d->efd < 0) {
        qdpn_log_errno(d, "Can't create eventfd");
        exit(1);
    }
#endif

    return d;
}

void qdpn_driver_free(qdpn_driver_t *d)
{
    if (!d) return;

#ifdef __sun
    close(d->ctrl[0]);
    close(d->ctrl[1]);
#else
    close(d->efd);
#endif

    qdpn_connector_t *conn = DEQ_HEAD(d->connectors);

    while (conn) {
        qdpn_connector_free(conn);
        conn = DEQ_HEAD(d->connectors);
    }

    qdpn_listener_t *listener = DEQ_HEAD(d->listeners);

    while (listener) {
        qdpn_listener_free(listener);
        listener = DEQ_HEAD(d->listeners);
    }

    free(d->fds);
    sys_mutex_free(d->lock);
    free(d);
}

int qdpn_driver_wakeup(qdpn_driver_t *d)
{
#ifdef __sun
    if (d) {
        ssize_t count = write(d->ctrl[1], "x", 1);
        if (count <= 0) {
            return count;
        } else {
            return 0;
        }
    } else {
        return PN_ARG_ERR;
    }
#else
    static uint64_t efd_delta = 1;

    if (d)
        ignore_result(write(d->efd, &efd_delta, sizeof(uint64_t)));
    return 0;
#endif
}

static void qdpn_driver_rebuild(qdpn_driver_t *d)
{
    sys_mutex_lock(d->lock);
    size_t size = DEQ_SIZE(d->listeners) + DEQ_SIZE(d->connectors);
    if (d->capacity < size + 1) {
        d->capacity = d->capacity > 16 ? d->capacity : 16;
        while (d->capacity < size + 1) {
            d->capacity *= 2;
        }
        d->fds = (struct pollfd *) realloc(d->fds, d->capacity*sizeof(struct pollfd));
    }


    d->wakeup = 0;
    d->nfds = 0;

#ifdef __sun
    d->fds[d->nfds].fd = d->ctrl[0];
#else
    d->fds[d->nfds].fd = d->efd;
#endif
    d->fds[d->nfds].events = POLLIN;
    d->fds[d->nfds].revents = 0;
    d->nfds++;

    qdpn_listener_t *l = DEQ_HEAD(d->listeners);
    while (l) {
        d->fds[d->nfds].fd = l->fd;
        d->fds[d->nfds].events = POLLIN;
        d->fds[d->nfds].revents = 0;
        l->idx = d->nfds;
        d->nfds++;
        l = DEQ_NEXT(l);
    }

    qdpn_connector_t *c = DEQ_HEAD(d->connectors);
    while (c) {
        if (!c->closed && !c->socket_error && !c->hangup) {
            d->wakeup = pn_timestamp_min(d->wakeup, c->wakeup);
            d->fds[d->nfds].fd = c->fd;
            d->fds[d->nfds].events = (c->status & PN_SEL_RD ? POLLIN : 0) | (c->status & PN_SEL_WR ? POLLOUT : 0);
            d->fds[d->nfds].revents = 0;
            c->idx = d->nfds;
            d->nfds++;
        }
        c = DEQ_NEXT(c);
    }

    sys_mutex_unlock(d->lock);
}

void qdpn_driver_wait_1(qdpn_driver_t *d)
{
    qdpn_driver_rebuild(d);
}

int qdpn_driver_wait_2(qdpn_driver_t *d, int timeout)
{
    if (d->wakeup) {
        pn_timestamp_t now = pn_i_now();
        if (now >= d->wakeup)
            timeout = 0;
        else
            timeout = (timeout < 0) ? d->wakeup-now : pn_min(timeout, d->wakeup - now);
    }
    int result = poll(d->fds, d->nfds, d->closed_count > 0 ? 0 : timeout);
    if (result == -1 && errno != EINTR)
        qdpn_log_errno(d, "poll");
    return result;
}

int qdpn_driver_wait_3(qdpn_driver_t *d)
{
    bool woken = false;
    if (d->fds[0].revents & POLLIN) {
        woken = true;
#ifdef __sun
        //clear the pipe
        char buffer[512];
        while (read(d->ctrl[0], buffer, 512) == 512);
#else
        char buffer[sizeof(uint64_t)];
        ignore_result(read(d->efd, buffer, sizeof(uint64_t)));
#endif
    }

    sys_mutex_lock(d->lock);
    qdpn_listener_t *l = DEQ_HEAD(d->listeners);
    while (l) {
        l->pending = (l->idx && d->fds[l->idx].revents & POLLIN);
        l = DEQ_NEXT(l);
    }

    pn_timestamp_t now = pn_i_now();
    qdpn_connector_t *c = DEQ_HEAD(d->connectors);
    while (c) {
        if (c->closed) {
            c->pending_read = false;
            c->pending_write = false;
            c->pending_tick = false;
        } else if (c->idx) {
            short revents = d->fds[c->idx].revents;
            c->pending_read = (revents & POLLIN);
            c->pending_write = (revents & POLLOUT);
            c->socket_error = (revents & POLLERR);
            c->pending_tick = (c->wakeup &&  c->wakeup <= now);
            if (revents & ~(POLLIN|POLLOUT|POLLERR|POLLHUP)) {
                qd_log(c->driver->log, QD_LOG_ERROR, "unexpected poll events %04x on %s",
                       revents, c->name);
                c->socket_error = true;
            }
            if (revents & POLLHUP) {
                c->hangup = true;
                /* poll() is signalling POLLHUP. To see what happened we need
                 * to do an actual recv() to get the error code. But we might
                 * be in a state where we're not interested in input, in that
                 * case try to get the error code via send() */
                short events = d->fds[c->idx].events;
                if (events & POLLIN) c->pending_read = true;
                else if (events & POLLOUT) c->pending_write = true;
            }
        }
        c = DEQ_NEXT(c);
    }

    //
    // Rotate the head connector to the tail.  This improves the fairness of polling on
    // open FDs.
    //
    c = DEQ_HEAD(d->connectors);
    if (c) {
        DEQ_REMOVE_HEAD(d->connectors);
        DEQ_INSERT_TAIL(d->connectors, c);
    }

    d->listener_next = DEQ_HEAD(d->listeners);
    d->connector_next = DEQ_HEAD(d->connectors);
    sys_mutex_unlock(d->lock);

    return woken ? PN_INTR : 0;
}

//
// XXX - pn_driver_wait has been divided into three internal functions as a
//       temporary workaround for a multi-threading problem.  A multi-threaded
//       application must hold a lock on parts 1 and 3, but not on part 2.
//       This temporary change, which is not reflected in the driver's API, allows
//       a multi-threaded application to use the three parts separately.
//
//       This workaround will eventually be replaced by a more elegant solution
//       to the problem.
//
int qdpn_driver_wait(qdpn_driver_t *d, int timeout)
{
    qdpn_driver_wait_1(d);
    int result = qdpn_driver_wait_2(d, timeout);
    if (result == -1)
        return errno;
    return qdpn_driver_wait_3(d);
}

qdpn_listener_t *qdpn_driver_listener(qdpn_driver_t *d)
{
    if (!d) return NULL;

    sys_mutex_lock(d->lock);
    while (d->listener_next) {
        qdpn_listener_t *l = d->listener_next;
        d->listener_next = DEQ_NEXT(l);

        if (l->pending) {
            sys_mutex_unlock(d->lock);
            return l;
        }
    }

    sys_mutex_unlock(d->lock);
    return NULL;
}

qdpn_connector_t *qdpn_driver_connector(qdpn_driver_t *d)
{
    if (!d) return NULL;

    sys_mutex_lock(d->lock);
    while (d->connector_next) {
        qdpn_connector_t *c = d->connector_next;
        d->connector_next = DEQ_NEXT(c);

        if (c->closed || c->pending_read || c->pending_write || c->pending_tick || c->socket_error) {
            sys_mutex_unlock(d->lock);
            return c;
        }
    }

    sys_mutex_unlock(d->lock);
    return NULL;
}

void qdpn_connector_wakeup(qdpn_connector_t *c, pn_timestamp_t t) {
    c->wakeup = t;
}

void qdpn_connector_set_methods(qdpn_connector_t *c, qdpn_connector_methods_t *m) {
    c->methods = m;
}
