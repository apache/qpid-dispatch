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
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/agent.h>
#include <qpid/dispatch/log.h>
#include "server_private.h"
#include "timer_private.h"
#include "alloc_private.h"
#include "dispatch_private.h"
#include "work_queue.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

static char *module="SERVER";
static __thread qd_server_t *thread_server = 0;

typedef struct qd_thread_t {
    qd_server_t  *qd_server;
    int           thread_id;
    volatile int  running;
    volatile int  canceled;
    int           using_thread;
    sys_thread_t *thread;
} qd_thread_t;


struct qd_server_t {
    int                      thread_count;
    const char              *container_name;
    pn_driver_t             *driver;
    qd_thread_start_cb_t     start_handler;
    qd_conn_handler_cb_t     conn_handler;
    qd_user_fd_handler_cb_t  ufd_handler;
    void                    *start_context;
    void                    *conn_handler_context;
    sys_cond_t              *cond;
    sys_mutex_t             *lock;
    qd_thread_t            **threads;
    work_queue_t            *work_queue;
    qd_timer_list_t          pending_timers;
    bool                     a_thread_is_waiting;
    int                      threads_active;
    int                      pause_requests;
    int                      threads_paused;
    int                      pause_next_sequence;
    int                      pause_now_serving;
    qd_signal_handler_cb_t   signal_handler;
    void                    *signal_context;
    int                      pending_signal;
    qd_connection_list_t     connections;
};




ALLOC_DEFINE(qd_listener_t);
ALLOC_DEFINE(qd_connector_t);
ALLOC_DEFINE(qd_connection_t);
ALLOC_DEFINE(qd_user_fd_t);


static qd_thread_t *thread(qd_server_t *qd_server, int id)
{
    qd_thread_t *thread = NEW(qd_thread_t);
    if (!thread)
        return 0;

    thread->qd_server    = qd_server;
    thread->thread_id    = id;
    thread->running      = 0;
    thread->canceled     = 0;
    thread->using_thread = 0;

    return thread;
}


static void thread_process_listeners(qd_server_t *qd_server)
{
    pn_driver_t     *driver   = qd_server->driver;
    pn_listener_t   *listener = pn_driver_listener(driver);
    pn_connector_t  *cxtr;
    qd_connection_t *ctx;

    while (listener) {
        cxtr = pn_listener_accept(listener);
        qd_log(module, QD_LOG_TRACE, "Accepting Connection from %s", pn_connector_name(cxtr));
        ctx = new_qd_connection_t();
        DEQ_ITEM_INIT(ctx);
        ctx->state        = CONN_STATE_OPENING;
        ctx->owner_thread = CONTEXT_NO_OWNER;
        ctx->enqueued     = 0;
        ctx->pn_cxtr      = cxtr;
        ctx->listener     = (qd_listener_t*) pn_listener_context(listener);
        ctx->connector    = 0;
        ctx->context      = ctx->listener->context;
        ctx->user_context = 0;
        ctx->link_context = 0;
        ctx->ufd          = 0;

        pn_connection_t *conn = pn_connection();
        pn_connection_set_container(conn, qd_server->container_name);
        pn_connector_set_connection(cxtr, conn);
        pn_connection_set_context(conn, ctx);
        ctx->pn_conn = conn;

        qd_log(module, QD_LOG_DEBUG, "added listener connection");
        // qd_server->lock is already locked
        DEQ_INSERT_TAIL(qd_server->connections, ctx);

        //
        // Get a pointer to the transport so we can insert security components into it
        //
        pn_transport_t           *tport  = pn_connector_transport(cxtr);
        const qd_server_config_t *config = ctx->listener->config;

        //
        // Set up SSL if appropriate
        //
        if (config->ssl_enabled) {
            pn_ssl_domain_t *domain = pn_ssl_domain(PN_SSL_MODE_SERVER);
            pn_ssl_domain_set_credentials(domain,
                                          config->ssl_certificate_file,
                                          config->ssl_private_key_file,
                                          config->ssl_password);
            if (config->ssl_allow_unsecured_client)
                pn_ssl_domain_allow_unsecured_client(domain);

            if (config->ssl_trusted_certificate_db)
                pn_ssl_domain_set_trusted_ca_db(domain, config->ssl_trusted_certificate_db);

            const char *trusted = config->ssl_trusted_certificate_db;
            if (config->ssl_trusted_certificates)
                trusted = config->ssl_trusted_certificates;

            if (config->ssl_require_peer_authentication)
                pn_ssl_domain_set_peer_authentication(domain, PN_SSL_VERIFY_PEER_NAME, trusted);

            pn_ssl_t *ssl = pn_ssl(tport);
            pn_ssl_init(ssl, domain, 0);
            pn_ssl_domain_free(domain);
        }

        //
        // Set up SASL
        //
        pn_sasl_t *sasl = pn_sasl(tport);
        pn_sasl_mechanisms(sasl, config->sasl_mechanisms);
        pn_sasl_server(sasl);
        pn_sasl_done(sasl, PN_SASL_OK);  // TODO - This needs to go away

        pn_connector_set_context(cxtr, ctx);
        listener = pn_driver_listener(driver);
    }
}


static void handle_signals_LH(qd_server_t *qd_server)
{
    int signum = qd_server->pending_signal;

    if (signum) {
        qd_server->pending_signal = 0;
        if (qd_server->signal_handler) {
            sys_mutex_unlock(qd_server->lock);
            qd_server->signal_handler(qd_server->signal_context, signum);
            sys_mutex_lock(qd_server->lock);
        }
    }
}


static void block_if_paused_LH(qd_server_t *qd_server)
{
    if (qd_server->pause_requests > 0) {
        qd_server->threads_paused++;
        sys_cond_signal_all(qd_server->cond);
        while (qd_server->pause_requests > 0)
            sys_cond_wait(qd_server->cond, qd_server->lock);
        qd_server->threads_paused--;
    }
}


static int process_connector(qd_server_t *qd_server, pn_connector_t *cxtr)
{
    qd_connection_t *ctx = pn_connector_context(cxtr);
    int events = 0;
    int passes = 0;

    if (ctx->state == CONN_STATE_USER) {
        qd_server->ufd_handler(ctx->ufd->context, ctx->ufd);
        return 1;
    }

    do {
        passes++;

        //
        // Step the engine for pre-handler processing
        //
        pn_connector_process(cxtr);

        //
        // Call the handler that is appropriate for the connector's state.
        //
        switch (ctx->state) {
        case CONN_STATE_CONNECTING: {
            if (pn_connector_closed(cxtr)) {
                ctx->state = CONN_STATE_FAILED;
                events = 0;
                break;
            }

            pn_connection_t *conn = pn_connection();
            pn_connection_set_container(conn, qd_server->container_name);
            pn_connector_set_connection(cxtr, conn);
            pn_connection_set_context(conn, ctx);
            ctx->pn_conn = conn;

            pn_transport_t           *tport  = pn_connector_transport(cxtr);
            const qd_server_config_t *config = ctx->connector->config;

            //
            // Set up SSL if appropriate
            //
            if (config->ssl_enabled) {
                pn_ssl_domain_t *domain = pn_ssl_domain(PN_SSL_MODE_CLIENT);
                pn_ssl_domain_set_credentials(domain,
                                              config->ssl_certificate_file,
                                              config->ssl_private_key_file,
                                              config->ssl_password);

                if (config->ssl_require_peer_authentication)
                    pn_ssl_domain_set_peer_authentication(domain, PN_SSL_VERIFY_PEER_NAME, config->ssl_trusted_certificate_db);

                pn_ssl_t *ssl = pn_ssl(tport);
                pn_ssl_init(ssl, domain, 0);
                pn_ssl_domain_free(domain);
            }

            //
            // Set up SASL
            //
            pn_sasl_t *sasl = pn_sasl(tport);
            pn_sasl_mechanisms(sasl, config->sasl_mechanisms);
            pn_sasl_client(sasl);

            ctx->state = CONN_STATE_OPENING;
            assert(ctx->connector);
            ctx->connector->state = CXTR_STATE_OPEN;
            events = 1;
            break;
        }

        case CONN_STATE_OPENING: {
            pn_transport_t *tport = pn_connector_transport(cxtr);
            pn_sasl_t      *sasl  = pn_sasl(tport);

            if (pn_sasl_outcome(sasl) == PN_SASL_OK) {
                ctx->state = CONN_STATE_OPERATIONAL;

                qd_conn_event_t ce = QD_CONN_EVENT_PROCESS; // Initialize to keep the compiler happy

                if (ctx->listener) {
                    ce = QD_CONN_EVENT_LISTENER_OPEN;
                } else if (ctx->connector) {
                    ce = QD_CONN_EVENT_CONNECTOR_OPEN;
                    ctx->connector->delay = 0;
                } else
                    assert(0);

                qd_server->conn_handler(qd_server->conn_handler_context,
                                        ctx->context, ce, (qd_connection_t*) pn_connector_context(cxtr));
                events = 1;
                break;
            }
            else if (pn_sasl_outcome(sasl) != PN_SASL_NONE) {
                ctx->state = CONN_STATE_FAILED;
                if (ctx->connector) {
                    const qd_server_config_t *config = ctx->connector->config;
                    qd_log(module, QD_LOG_TRACE, "Connection to %s:%s failed", config->host, config->port);
                }
            }
        }

        case CONN_STATE_OPERATIONAL:
            if (pn_connector_closed(cxtr)) {
                qd_server->conn_handler(qd_server->conn_handler_context, ctx->context,
                                        QD_CONN_EVENT_CLOSE,
                                        (qd_connection_t*) pn_connector_context(cxtr));
                events = 0;
            }
            else
                events = qd_server->conn_handler(qd_server->conn_handler_context, ctx->context,
                                                 QD_CONN_EVENT_PROCESS,
                                                 (qd_connection_t*) pn_connector_context(cxtr));
            break;

        default:
            break;
        }
    } while (events > 0);

    return passes > 1;
}


//
// TEMPORARY FUNCTION PROTOTYPES
//
void pn_driver_wait_1(pn_driver_t *d);
int  pn_driver_wait_2(pn_driver_t *d, int timeout);
void pn_driver_wait_3(pn_driver_t *d);
//
// END TEMPORARY
//

static void *thread_run(void *arg)
{
    qd_thread_t     *thread    = (qd_thread_t*) arg;
    qd_server_t     *qd_server = thread->qd_server;
    pn_connector_t  *work;
    pn_connection_t *conn;
    qd_connection_t *ctx;
    int              error;
    int              poll_result;

    if (!thread)
        return 0;

    thread_server   = qd_server;
    thread->running = 1;

    if (thread->canceled)
        return 0;

    //
    // Invoke the start handler if the application supplied one.
    // This handler can be used to set NUMA or processor affinnity for the thread.
    //
    if (qd_server->start_handler)
        qd_server->start_handler(qd_server->start_context, thread->thread_id);

    //
    // Main Loop
    //
    while (thread->running) {
        sys_mutex_lock(qd_server->lock);

        //
        // Check for pending signals to process
        //
        handle_signals_LH(qd_server);
        if (!thread->running) {
            sys_mutex_unlock(qd_server->lock);
            break;
        }

        //
        // Check to see if the server is pausing.  If so, block here.
        //
        block_if_paused_LH(qd_server);
        if (!thread->running) {
            sys_mutex_unlock(qd_server->lock);
            break;
        }

        //
        // Service pending timers.
        //
        qd_timer_t *timer = DEQ_HEAD(qd_server->pending_timers);
        if (timer) {
            DEQ_REMOVE_HEAD(qd_server->pending_timers);

            //
            // Mark the timer as idle in case it reschedules itself.
            //
            qd_timer_idle_LH(timer);

            //
            // Release the lock and invoke the connection handler.
            //
            sys_mutex_unlock(qd_server->lock);
            timer->handler(timer->context);
            pn_driver_wakeup(qd_server->driver);
            continue;
        }

        //
        // Check the work queue for connectors scheduled for processing.
        //
        work = work_queue_get(qd_server->work_queue);
        if (!work) {
            //
            // There is no pending work to do
            //
            if (qd_server->a_thread_is_waiting) {
                //
                // Another thread is waiting on the proton driver, this thread must
                // wait on the condition variable until signaled.
                //
                sys_cond_wait(qd_server->cond, qd_server->lock);
            } else {
                //
                // This thread elects itself to wait on the proton driver.  Set the
                // thread-is-waiting flag so other idle threads will not interfere.
                //
                qd_server->a_thread_is_waiting = true;

                //
                // Ask the timer module when its next timer is scheduled to fire.  We'll
                // use this value in driver_wait as the timeout.  If there are no scheduled
                // timers, the returned value will be -1.
                //
                long duration = qd_timer_next_duration_LH();

                //
                // Invoke the proton driver's wait sequence.  This is a bit of a hack for now
                // and will be improved in the future.  The wait process is divided into three parts,
                // the first and third of which need to be non-reentrant, and the second of which
                // must be reentrant (and blocks).
                //
                pn_driver_wait_1(qd_server->driver);
                sys_mutex_unlock(qd_server->lock);

                do {
                    error = 0;
                    poll_result = pn_driver_wait_2(qd_server->driver, duration);
                    if (poll_result == -1)
                        error = pn_driver_errno(qd_server->driver);
                } while (error == PN_INTR);
                if (error) {
                    qd_log(module, QD_LOG_ERROR, "Driver Error: %s", pn_driver_error(qd_server->driver));
                    exit(-1);
                }

                sys_mutex_lock(qd_server->lock);
                pn_driver_wait_3(qd_server->driver);

                if (!thread->running) {
                    sys_mutex_unlock(qd_server->lock);
                    break;
                }

                //
                // Visit the timer module.
                //
                struct timespec tv;
                clock_gettime(CLOCK_REALTIME, &tv);
                long milliseconds = tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
                qd_timer_visit_LH(milliseconds);

                //
                // Process listeners (incoming connections).
                //
                thread_process_listeners(qd_server);

                //
                // Traverse the list of connectors-needing-service from the proton driver.
                // If the connector is not already in the work queue and it is not currently
                // being processed by another thread, put it in the work queue and signal the
                // condition variable.
                //
                work = pn_driver_connector(qd_server->driver);
                while (work) {
                    ctx = pn_connector_context(work);
                    if (!ctx->enqueued && ctx->owner_thread == CONTEXT_NO_OWNER) {
                        ctx->enqueued = 1;
                        work_queue_put(qd_server->work_queue, work);
                        sys_cond_signal(qd_server->cond);
                    }
                    work = pn_driver_connector(qd_server->driver);
                }

                //
                // Release our exclusive claim on pn_driver_wait.
                //
                qd_server->a_thread_is_waiting = false;
            }
        }

        //
        // If we were given a connector to work on from the work queue, mark it as
        // owned by this thread and as no longer enqueued.
        //
        if (work) {
            ctx = pn_connector_context(work);
            if (ctx->owner_thread == CONTEXT_NO_OWNER) {
                ctx->owner_thread = thread->thread_id;
                ctx->enqueued = 0;
                qd_server->threads_active++;
            } else {
                //
                // This connector is being processed by another thread, re-queue it.
                //
                work_queue_put(qd_server->work_queue, work);
                work = 0;
            }
        }
        sys_mutex_unlock(qd_server->lock);

        //
        // Process the connector that we now have exclusive access to.
        //
        if (work) {
            int work_done = process_connector(qd_server, work);

            //
            // Check to see if the connector was closed during processing
            //
            if (pn_connector_closed(work)) {
                //
                // Connector is closed.  Free the context and the connector.
                //
                conn = pn_connector_connection(work);

                //
                // If this is a dispatch connector, schedule the re-connect timer
                //
                if (ctx->connector) {
                    ctx->connector->ctx = 0;
                    ctx->connector->state = CXTR_STATE_CONNECTING;
                    qd_timer_schedule(ctx->connector->timer, ctx->connector->delay);
                }

                sys_mutex_lock(qd_server->lock);
                DEQ_REMOVE(qd_server->connections, ctx);
                qd_log(module, QD_LOG_DEBUG, "removed %s connection",
                        ctx->connector ? "connector" : "listener");
                free_qd_connection_t(ctx);
                pn_connector_free(work);
                if (conn)
                    pn_connection_free(conn);
                qd_server->threads_active--;
                sys_mutex_unlock(qd_server->lock);
            } else {
                //
                // The connector lives on.  Mark it as no longer owned by this thread.
                //
                sys_mutex_lock(qd_server->lock);
                ctx->owner_thread = CONTEXT_NO_OWNER;
                qd_server->threads_active--;
                sys_mutex_unlock(qd_server->lock);
            }

            //
            // Wake up the proton driver to force it to reconsider its set of FDs
            // in light of the processing that just occurred.
            //
            if (work_done)
                pn_driver_wakeup(qd_server->driver);
        }
    }

    return 0;
}


static void thread_start(qd_thread_t *thread)
{
    if (!thread)
        return;

    thread->using_thread = 1;
    thread->thread = sys_thread(thread_run, (void*) thread);
}


static void thread_cancel(qd_thread_t *thread)
{
    if (!thread)
        return;

    thread->running  = 0;
    thread->canceled = 1;
}


static void thread_join(qd_thread_t *thread)
{
    if (!thread)
        return;

    if (thread->using_thread)
        sys_thread_join(thread->thread);
}


static void thread_free(qd_thread_t *thread)
{
    if (!thread)
        return;

    free(thread);
}


static void cxtr_try_open(void *context)
{
    qd_connector_t *ct = (qd_connector_t*) context;
    if (ct->state != CXTR_STATE_CONNECTING)
        return;

    qd_connection_t *ctx = new_qd_connection_t();
    DEQ_ITEM_INIT(ctx);
    ctx->server       = ct->server;
    ctx->state        = CONN_STATE_CONNECTING;
    ctx->owner_thread = CONTEXT_NO_OWNER;
    ctx->enqueued     = 0;
    ctx->pn_conn      = 0;
    ctx->listener     = 0;
    ctx->connector    = ct;
    ctx->context      = ct->context;
    ctx->user_context = 0;
    ctx->link_context = 0;
    ctx->ufd          = 0;

    //
    // pn_connector is not thread safe
    //
    sys_mutex_lock(ct->server->lock);
    ctx->pn_cxtr = pn_connector(ct->server->driver, ct->config->host, ct->config->port, (void*) ctx);
    DEQ_INSERT_TAIL(ct->server->connections, ctx);
    qd_log(module, QD_LOG_DEBUG, "added connector connection");
    sys_mutex_unlock(ct->server->lock);

    ct->ctx   = ctx;
    ct->delay = 5000;
    qd_log(module, QD_LOG_TRACE, "Connecting to %s:%s", ct->config->host, ct->config->port);
}


qd_server_t *qd_server(int thread_count, const char *container_name)
{
    int i;

    qd_server_t *qd_server = NEW(qd_server_t);
    if (qd_server == 0)
        return 0;

    DEQ_INIT(qd_server->connections);
    qd_server->thread_count    = thread_count;
    qd_server->container_name  = container_name;
    qd_server->driver          = pn_driver();
    qd_server->start_handler   = 0;
    qd_server->conn_handler    = 0;
    qd_server->signal_handler  = 0;
    qd_server->ufd_handler     = 0;
    qd_server->start_context   = 0;
    qd_server->signal_context  = 0;
    qd_server->lock            = sys_mutex();
    qd_server->cond            = sys_cond();

    qd_timer_initialize(qd_server->lock);

    qd_server->threads = NEW_PTR_ARRAY(qd_thread_t, thread_count);
    for (i = 0; i < thread_count; i++)
        qd_server->threads[i] = thread(qd_server, i);

    qd_server->work_queue          = work_queue();
    DEQ_INIT(qd_server->pending_timers);
    qd_server->a_thread_is_waiting = false;
    qd_server->threads_active      = 0;
    qd_server->pause_requests      = 0;
    qd_server->threads_paused      = 0;
    qd_server->pause_next_sequence = 0;
    qd_server->pause_now_serving   = 0;
    qd_server->pending_signal      = 0;

    qd_log(module, QD_LOG_INFO, "Container Name: %s", qd_server->container_name);

    return qd_server;
}


void qd_server_free(qd_server_t *qd_server)
{
    int i;
    if (!qd_server)
        return;

    for (i = 0; i < qd_server->thread_count; i++)
        thread_free(qd_server->threads[i]);

    work_queue_free(qd_server->work_queue);

    pn_driver_free(qd_server->driver);
    sys_mutex_free(qd_server->lock);
    sys_cond_free(qd_server->cond);
    free(qd_server);
}


void qd_server_set_conn_handler(qd_dispatch_t *qd, qd_conn_handler_cb_t handler, void *handler_context)
{
    qd->server->conn_handler         = handler;
    qd->server->conn_handler_context = handler_context;
}


void qd_server_set_signal_handler(qd_dispatch_t *qd, qd_signal_handler_cb_t handler, void *context)
{
    qd->server->signal_handler = handler;
    qd->server->signal_context = context;
}


void qd_server_set_start_handler(qd_dispatch_t *qd, qd_thread_start_cb_t handler, void *context)
{
    qd->server->start_handler = handler;
    qd->server->start_context = context;
}


void qd_server_set_user_fd_handler(qd_dispatch_t *qd, qd_user_fd_handler_cb_t ufd_handler)
{
    qd->server->ufd_handler = ufd_handler;
}


static void qd_server_announce(qd_server_t* qd_server)
{
    qd_log(module, QD_LOG_INFO, "Operational, %d Threads Running", qd_server->thread_count);
#ifndef NDEBUG
    qd_log(module, QD_LOG_INFO, "Running in DEBUG Mode");
#endif
}


void qd_server_run(qd_dispatch_t *qd)
{
    qd_server_t *qd_server = qd->server;

    int i;
    if (!qd_server)
        return;

    assert(qd_server->conn_handler); // Server can't run without a connection handler.

    for (i = 1; i < qd_server->thread_count; i++)
        thread_start(qd_server->threads[i]);

    qd_server_announce(qd_server);

    thread_run((void*) qd_server->threads[0]);

    for (i = 1; i < qd_server->thread_count; i++)
        thread_join(qd_server->threads[i]);

    qd_log(module, QD_LOG_INFO, "Shut Down");
}


void qd_server_start(qd_dispatch_t *qd)
{
    qd_server_t *qd_server = qd->server;
    int i;

    if (!qd_server)
        return;

    assert(qd_server->conn_handler); // Server can't run without a connection handler.

    for (i = 0; i < qd_server->thread_count; i++)
        thread_start(qd_server->threads[i]);

    qd_server_announce(qd_server);
}


void qd_server_stop(qd_dispatch_t *qd)
{
    qd_server_t *qd_server = qd->server;
    int idx;

    sys_mutex_lock(qd_server->lock);
    for (idx = 0; idx < qd_server->thread_count; idx++)
        thread_cancel(qd_server->threads[idx]);
    sys_cond_signal_all(qd_server->cond);
    pn_driver_wakeup(qd_server->driver);
    sys_mutex_unlock(qd_server->lock);

    if (thread_server != qd_server) {
        for (idx = 0; idx < qd_server->thread_count; idx++)
            thread_join(qd_server->threads[idx]);
        qd_log(module, QD_LOG_INFO, "Shut Down");
    }
}


void qd_server_signal(qd_dispatch_t *qd, int signum)
{
    qd_server_t *qd_server = qd->server;

    qd_server->pending_signal = signum;
    sys_cond_signal_all(qd_server->cond);
    pn_driver_wakeup(qd_server->driver);
}


void qd_server_pause(qd_dispatch_t *qd)
{
    qd_server_t *qd_server = qd->server;

    sys_mutex_lock(qd_server->lock);

    //
    // Bump the request count to stop all the threads.
    //
    qd_server->pause_requests++;
    int my_sequence = qd_server->pause_next_sequence++;

    //
    // Awaken all threads that are currently blocking.
    //
    sys_cond_signal_all(qd_server->cond);
    pn_driver_wakeup(qd_server->driver);

    //
    // Wait for the paused thread count plus the number of threads requesting a pause to equal
    // the total thread count.  Also, don't exit the blocking loop until now_serving equals our
    // sequence number.  This ensures that concurrent pausers don't run at the same time.
    //
    while ((qd_server->threads_paused + qd_server->pause_requests < qd_server->thread_count) ||
           (my_sequence != qd_server->pause_now_serving))
        sys_cond_wait(qd_server->cond, qd_server->lock);

    sys_mutex_unlock(qd_server->lock);
}


void qd_server_resume(qd_dispatch_t *qd)
{
    qd_server_t *qd_server = qd->server;

    sys_mutex_lock(qd_server->lock);
    qd_server->pause_requests--;
    qd_server->pause_now_serving++;
    sys_cond_signal_all(qd_server->cond);
    sys_mutex_unlock(qd_server->lock);
}


void qd_server_activate(qd_connection_t *ctx)
{
    if (!ctx)
        return;

    pn_connector_t *ctor = ctx->pn_cxtr;
    if (!ctor)
        return;

    if (!pn_connector_closed(ctor))
        pn_connector_activate(ctor, PN_CONNECTOR_WRITABLE);
}


void qd_connection_set_context(qd_connection_t *conn, void *context)
{
    conn->user_context = context;
}


void *qd_connection_get_context(qd_connection_t *conn)
{
    return conn->user_context;
}


void qd_connection_set_link_context(qd_connection_t *conn, void *context)
{
    conn->link_context = context;
}


void *qd_connection_get_link_context(qd_connection_t *conn)
{
    return conn->link_context;
}


pn_connection_t *qd_connection_pn(qd_connection_t *conn)
{
    return conn->pn_conn;
}


const qd_server_config_t *qd_connection_config(const qd_connection_t *conn)
{
    if (conn->listener)
        return conn->listener->config;
    return conn->connector->config;
}


qd_listener_t *qd_server_listen(qd_dispatch_t *qd, const qd_server_config_t *config, void *context)
{
    qd_server_t   *qd_server = qd->server;
    qd_listener_t *li        = new_qd_listener_t();

    if (!li)
        return 0;

    li->server      = qd_server;
    li->config      = config;
    li->context     = context;
    li->pn_listener = pn_listener(qd_server->driver, config->host, config->port, (void*) li);

    if (!li->pn_listener) {
        qd_log(module, QD_LOG_ERROR, "Driver Error %d (%s)",
               pn_driver_errno(qd_server->driver), pn_driver_error(qd_server->driver));
        free_qd_listener_t(li);
        return 0;
    }
    qd_log(module, QD_LOG_TRACE, "Listening on %s:%s", config->host, config->port);

    return li;
}


void qd_server_listener_free(qd_listener_t* li)
{
    pn_listener_free(li->pn_listener);
    free_qd_listener_t(li);
}


void qd_server_listener_close(qd_listener_t* li)
{
    pn_listener_close(li->pn_listener);
}


qd_connector_t *qd_server_connect(qd_dispatch_t *qd, const qd_server_config_t *config, void *context)
{
    qd_server_t    *qd_server = qd->server;
    qd_connector_t *ct        = new_qd_connector_t();

    if (!ct)
        return 0;

    ct->server  = qd_server;
    ct->state   = CXTR_STATE_CONNECTING;
    ct->config  = config;
    ct->context = context;
    ct->ctx     = 0;
    ct->timer   = qd_timer(qd, cxtr_try_open, (void*) ct);
    ct->delay   = 0;

    qd_timer_schedule(ct->timer, ct->delay);
    return ct;
}


void qd_server_connector_free(qd_connector_t* ct)
{
    // Don't free the proton connector.  This will be done by the connector
    // processing/cleanup.

    if (ct->ctx) {
        pn_connector_close(ct->ctx->pn_cxtr);
        ct->ctx->connector = 0;
    }

    qd_timer_free(ct->timer);
    free_qd_connector_t(ct);
}


qd_user_fd_t *qd_user_fd(qd_dispatch_t *qd, int fd, void *context)
{
    qd_server_t  *qd_server = qd->server;
    qd_user_fd_t *ufd       = new_qd_user_fd_t();

    if (!ufd)
        return 0;

    qd_connection_t *ctx = new_qd_connection_t();
    DEQ_ITEM_INIT(ctx);
    ctx->server       = qd_server;
    ctx->state        = CONN_STATE_USER;
    ctx->owner_thread = CONTEXT_NO_OWNER;
    ctx->enqueued     = 0;
    ctx->pn_conn      = 0;
    ctx->listener     = 0;
    ctx->connector    = 0;
    ctx->context      = 0;
    ctx->user_context = 0;
    ctx->link_context = 0;
    ctx->ufd          = ufd;

    ufd->context = context;
    ufd->server  = qd_server;
    ufd->fd      = fd;
    ufd->pn_conn = pn_connector_fd(qd_server->driver, fd, (void*) ctx);
    pn_driver_wakeup(qd_server->driver);

    return ufd;
}


void qd_user_fd_free(qd_user_fd_t *ufd)
{
    pn_connector_close(ufd->pn_conn);
    free_qd_user_fd_t(ufd);
}


void qd_user_fd_activate_read(qd_user_fd_t *ufd)
{
    pn_connector_activate(ufd->pn_conn, PN_CONNECTOR_READABLE);
    pn_driver_wakeup(ufd->server->driver);
}


void qd_user_fd_activate_write(qd_user_fd_t *ufd)
{
    pn_connector_activate(ufd->pn_conn, PN_CONNECTOR_WRITABLE);
    pn_driver_wakeup(ufd->server->driver);
}


bool qd_user_fd_is_readable(qd_user_fd_t *ufd)
{
    return pn_connector_activated(ufd->pn_conn, PN_CONNECTOR_READABLE);
}


bool qd_user_fd_is_writeable(qd_user_fd_t *ufd)
{
    return pn_connector_activated(ufd->pn_conn, PN_CONNECTOR_WRITABLE);
}


void qd_server_timer_pending_LH(qd_timer_t *timer)
{
    DEQ_INSERT_TAIL(timer->server->pending_timers, timer);
}


void qd_server_timer_cancel_LH(qd_timer_t *timer)
{
    DEQ_REMOVE(timer->server->pending_timers, timer);
}


static void server_schema_handler(void *context, void *correlator)
{
}


static void server_query_handler(void* context, const char *id, void *cor)
{
    qd_server_t *qd_server = (qd_server_t*) context;
    sys_mutex_lock(qd_server->lock);
    const char               *conn_state;
    const qd_server_config_t *config;
    const char               *pn_container_name;
    const char               *direction;

    qd_connection_t *conn = DEQ_HEAD(qd_server->connections);
    while (conn) {
        switch (conn->state) {
        case CONN_STATE_CONNECTING:  conn_state = "Connecting";  break;
        case CONN_STATE_OPENING:     conn_state = "Opening";     break;
        case CONN_STATE_OPERATIONAL: conn_state = "Operational"; break;
        case CONN_STATE_FAILED:      conn_state = "Failed";      break;
        case CONN_STATE_USER:        conn_state = "User";        break;
        default:                     conn_state = "undefined";   break;
        }
        qd_agent_value_string(cor, "state", conn_state);
        // get remote container name using proton connection
        pn_container_name = pn_connection_remote_container(conn->pn_conn);
        if (pn_container_name)
            qd_agent_value_string(cor, "container", pn_container_name);
        else
            qd_agent_value_null(cor, "container");

        // and now for some config entries
        if (conn->connector) {
            config = conn->connector->config;
            direction = "out";
            char host[1000];
            strcpy(host, config->host);
            strcat(host, ":");
            strcat(host, config->port);
            qd_agent_value_string(cor, "host", host);
        } else {
            config = conn->listener->config;
            direction = "in";
            qd_agent_value_string(cor, "host", pn_connector_name(conn->pn_cxtr));
        }

        qd_agent_value_string(cor, "sasl", config->sasl_mechanisms);
        qd_agent_value_string(cor, "role", config->role);
        qd_agent_value_string(cor, "dir",  direction);

        conn = DEQ_NEXT(conn);
        qd_agent_value_complete(cor, conn != 0);
    }
    sys_mutex_unlock(qd_server->lock);
}


void qd_server_setup_agent(qd_dispatch_t *qd)
{
    qd_agent_register_class(qd, "org.apache.qpid.dispatch.connection", qd->server, server_schema_handler, server_query_handler);
}
