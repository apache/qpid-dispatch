#ifndef __dispatch_posix_driver_h__
#define __dispatch_posix_driver_h__ 1

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

#include <qpid/dispatch/log.h>

#include <proton/error.h>
#include <proton/sasl.h>
#include <proton/selectable.h>
#include <proton/ssl.h>
#include <proton/transport.h>
#include <proton/types.h>

/** @file
 * API for the Driver Layer.
 *
 * The driver library provides a simple implementation of a driver for
 * the proton engine. A driver is responsible for providing input,
 * output, and tick events to the bottom half of the engine API. See
 * pn_transport_input, pn_transport_output, and
 * pn_transport_tick. The driver also provides an interface for the
 * application to access the top half of the API when the state of the
 * engine may have changed due to I/O or timing events. Additionally
 * the driver incorporates the SASL engine as well in order to provide
 * a complete network stack: AMQP over SASL over TCP.
 *
 */

typedef struct qdpn_driver_t qdpn_driver_t;
typedef struct qdpn_listener_t qdpn_listener_t;
typedef struct qdpn_connector_t qdpn_connector_t;

typedef enum {
    QDPN_CONNECTOR_WRITABLE,
    QDPN_CONNECTOR_READABLE
} qdpn_activate_criteria_t;

/** Construct a driver
 *
 *  Call qdpn_driver_free() to release the driver object.
 *  @param log source to use for log messages, the driver does not have it's own.
 *  @return new driver object, NULL if error
 */
qdpn_driver_t *qdpn_driver(qd_log_source_t* log);

/** Return the most recent error code.
 *
 * @param[in] d the driver
 *
 * @return the most recent error text for d
 */
int qdpn_driver_errno(qdpn_driver_t *d);

/** Get additional error information associated with the driver.
 *
 * Whenever a driver operation fails, additional error information can
 * be obtained using this function. The error object that is returned
 * may also be used to clear the error condition.
 *
 * The pointer returned by this operation is valid until the
 * driver object is freed.
 *
 * @param[in] d the driver
 *
 * @return the driver's error object
 */
pn_error_t *qdpn_driver_error(qdpn_driver_t *d);

/** Force qdpn_driver_wait() to return
 *
 * @param[in] driver the driver to wake up
 *
 * @return zero on success, an error code on failure
 */
int qdpn_driver_wakeup(qdpn_driver_t *driver);

/** Wait for an active connector or listener
 *
 * @param[in] driver the driver to wait on
 * @param[in] timeout maximum time in milliseconds to wait, -1 means
 *                    infinite wait
 *
 * @return zero on success, an error code on failure
 */
int qdpn_driver_wait(qdpn_driver_t *driver, int timeout);

/** Get the next listener with pending data in the driver.
 *
 * @param[in] driver the driver
 * @return NULL if no active listener available
 */
qdpn_listener_t *qdpn_driver_listener(qdpn_driver_t *driver);

/** Get the next active connector in the driver.
 *
 * Returns the next connector with pending inbound data, available
 * capacity for outbound data, or pending tick.
 *
 * @param[in] driver the driver
 * @return NULL if no active connector available
 */
qdpn_connector_t *qdpn_driver_connector(qdpn_driver_t *driver);

/** Free the driver allocated via qdpn_driver, and all associated
 *  listeners and connectors.
 *
 * @param[in] driver the driver to free, no longer valid on
 *                   return
 */
void qdpn_driver_free(qdpn_driver_t *driver);


/** qdpn_listener - the server API **/

/** Construct a listener for the given address.
 *
 * @param[in] driver driver that will 'own' this listener
 * @param[in] host local host address to listen on
 * @param[in] port local port to listen on
 * @param[in] protocol family to use (IPv4 or IPv6 or 0). If 0 (zero) is passed in the protocol family will be automatically determined from the address
 * @param[in] context application-supplied, can be accessed via
 *                    qdpn_listener_context()
 * @param[in] methods to apply to new connectors.
 * @return a new listener on the given host:port, NULL if error
 */
qdpn_listener_t *qdpn_listener(qdpn_driver_t *driver,
                               const char *host,
                               const char *port,
                               const char *protocol_family,
                               void* context
                              );

/** Access the head listener for a driver.
 *
 * @param[in] driver the driver whose head listener will be returned
 *
 * @return the head listener for driver or NULL if there is none
 */
qdpn_listener_t *qdpn_listener_head(qdpn_driver_t *driver);

/** Access the next listener.
 *
 * @param[in] listener the listener whose next listener will be
 *            returned
 *
 * @return the next listener
 */
qdpn_listener_t *qdpn_listener_next(qdpn_listener_t *listener);

/** Accept a connection that is pending on the listener.
 *
 * @param[in] listener the listener to accept the connection on
 * @param[in] policy policy that holds absolute connection limits
 * @param[in] policy_fn function that accepts remote host name and returns
 *            decision to allow or deny this connection
 * @param[out] counted pointer to a bool set to true when the connection was
 *             counted against absolute connection limits
 * @return a new connector for the remote, or NULL on error
 */
qdpn_connector_t *qdpn_listener_accept(qdpn_listener_t *listener,
                                       void *policy,
                                       bool (*policy_fn)(void *, const char *),
                                       bool *counted);

/** Access the application context that is associated with the listener.
 *
 * @param[in] listener the listener whose context is to be returned
 * @return the application context that was passed to qdpn_listener() or
 *         qdpn_listener_fd()
 */
void *qdpn_listener_context(qdpn_listener_t *listener);

void qdpn_listener_set_context(qdpn_listener_t *listener, void *context);

/** Close the socket used by the listener.
 *
 * @param[in] listener the listener whose socket will be closed.
 */
void qdpn_listener_close(qdpn_listener_t *listener);

/** Frees the given listener.
 *
 * Assumes the listener's socket has been closed prior to call.
 *
 * @param[in] listener the listener object to free, no longer valid
 *            on return
 */
void qdpn_listener_free(qdpn_listener_t *listener);




/** qdpn_connector - the client API **/

/** Construct a connector to the given remote address.
 *
 * @param[in] driver owner of this connection.
 * @param[in] host remote host to connect to.
 * @param[in] port remote port to connect to.
 * @param[in] protocol family to use (IPv4 or IPv6 or 0). If 0 (zero) is passed in the protocol family will be automatically determined from the address
 * @param[in] context application supplied, can be accessed via
 *                    qdpn_connector_context() @return a new connector
 *                    to the given remote, or NULL on error.
 */
qdpn_connector_t *qdpn_connector(qdpn_driver_t *driver,
                                 const char *host,
                                 const char *port,
                                 const char *protocol_family,
                                 void* context);

/** Access the head connector for a driver.
 *
 * @param[in] driver the driver whose head connector will be returned
 *
 * @return the head connector for driver or NULL if there is none
 */
qdpn_connector_t *qdpn_connector_head(qdpn_driver_t *driver);

/** Access the next connector.
 *
 * @param[in] connector the connector whose next connector will be
 *            returned
 *
 * @return the next connector
 */
qdpn_connector_t *qdpn_connector_next(qdpn_connector_t *connector);

/** Service the given connector.
 *
 * Handle any inbound data, outbound data, or timing events pending on
 * the connector.
 *
 * @param[in] connector the connector to process.
 */
void qdpn_connector_process(qdpn_connector_t *connector);

/** Access the listener which opened this connector.
 *
 * @param[in] connector connector whose listener will be returned.
 * @return the listener which created this connector, or NULL if the
 *         connector has no listener (e.g. an outbound client
 *         connection)
 */
qdpn_listener_t *qdpn_connector_listener(qdpn_connector_t *connector);

/** Access the Authentication and Security context of the connector.
 *
 * @param[in] connector connector whose security context will be
 *                      returned
 * @return the Authentication and Security context for the connector,
 *         or NULL if none
 */
pn_sasl_t *qdpn_connector_sasl(qdpn_connector_t *connector);

/** Access the AMQP Connection associated with the connector.
 *
 * @param[in] connector the connector whose connection will be
 *                      returned
 * @return the connection context for the connector, or NULL if none
 */
pn_connection_t *qdpn_connector_connection(qdpn_connector_t *connector);

/** Assign the AMQP Connection associated with the connector.
 *
 * @param[in] connector the connector whose connection will be set.
 * @param[in] connection the connection to associate with the
 *                       connector
 */
void qdpn_connector_set_connection(qdpn_connector_t *connector, pn_connection_t *connection);

/** Access the application context that is associated with the
 *  connector.
 *
 * @param[in] connector the connector whose context is to be returned.
 * @return the application context that was passed to qdpn_connector()
 *         or qdpn_connector_fd()
 */
void *qdpn_connector_context(qdpn_connector_t *connector);

/** Assign a new application context to the connector.
 *
 * @param[in] connector the connector which will hold the context.
 * @param[in] context new application context to associate with the
 *                    connector
 */
void qdpn_connector_set_context(qdpn_connector_t *connector, void *context);

/** Access the name of the connector
 *
 * @param[in] connector the connector of interest
 * @return the name of the connector in the form of a null-terminated character string.
 */
const char *qdpn_connector_name(const qdpn_connector_t *connector);

/** Access the numeric host ip of the connector
 *
 * @param[in] connector the connector of interest
 * @return the numeric host ip address of the connector in the form of a null-terminated character string.
 */
const char *qdpn_connector_hostip(const qdpn_connector_t *connector);

/** Access the transport used by this connector.
 *
 * @param[in] connector connector whose transport will be returned
 * @return the transport, or NULL if none
 */
pn_transport_t *qdpn_connector_transport(qdpn_connector_t *connector);

/** Close the socket used by the connector.
 *
 * @param[in] connector the connector whose socket will be closed
 */
void qdpn_connector_close(qdpn_connector_t *connector);

/** Call when the socket is already closed, an the connector needs updating.
 *
 * @param[in] connector the connector whose socket has been closed
 */
void qdpn_connector_after_close(qdpn_connector_t *connector);


/** Socket has been closed externally, mark it closed.
 *
 * @param[in] connector the connector whose socket will be closed
 */
void qdpn_connector_mark_closed(qdpn_connector_t *connector);

/** Determine if the connector is closed.
 *
 * @return True if closed, otherwise false
 */
bool qdpn_connector_closed(qdpn_connector_t *connector);

bool qdpn_connector_failed(qdpn_connector_t *connector);


/** Destructor for the given connector.
 *
 * Assumes the connector's socket has been closed prior to call.
 *
 * @param[in] connector the connector object to free. No longer
 *                      valid on return
 */
void qdpn_connector_free(qdpn_connector_t *connector);

/** Activate a connector when a criteria is met
 *
 * Set a criteria for a connector (i.e. it's transport is writable) that, once met,
 * the connector shall be placed in the driver's work queue.
 *
 * @param[in] connector The connector object to activate
 * @param[in] criteria  The criteria that must be met prior to activating the connector
 */
void qdpn_connector_activate(qdpn_connector_t *connector, qdpn_activate_criteria_t criteria);

/** Activate all of the open file descriptors
 */
void qdpn_activate_all(qdpn_driver_t *driver);

/** Return the activation status of the connector for a criteria
 *
 * Return the activation status (i.e. readable, writable) for the connector.  This function
 * has the side-effect of canceling the activation of the criteria.
 *
 * Please note that this function must not be used for normal AMQP connectors.  It is only
 * used for connectors created so the driver can track non-AMQP file descriptors.  Such
 * connectors are never passed into qdpn_connector_process.
 *
 * @param[in] connector The connector object to activate
 * @param[in] criteria  The criteria to test.  "Is this the reason the connector appeared
 *                      in the work list?"
 * @return true iff the criteria is activated on the connector.
 */
bool qdpn_connector_activated(qdpn_connector_t *connector, qdpn_activate_criteria_t criteria);

/** True if the connector has received a hangup */
bool qdpn_connector_hangup(qdpn_connector_t *connector);

/** Create a listener using the existing file descriptor.
 *
 * @param[in] driver driver that will 'own' this listener
 * @param[in] fd existing socket for listener to listen on
 * @param[in] context application-supplied, can be accessed via
 *                    qdpn_listener_context()
 * @return a new listener on the given host:port, NULL if error
 */
qdpn_listener_t *qdpn_listener_fd(qdpn_driver_t *driver, pn_socket_t fd, void *context);

pn_socket_t qdpn_listener_get_fd(qdpn_listener_t *listener);

/** Create a connector using the existing file descriptor.
 *
 * @param[in] driver driver that will 'own' this connector.
 * @param[in] fd existing socket to use for this connector.
 * @param[in] context application-supplied, can be accessed via
 *                    qdpn_connector_context()
 * @return a new connector to the given host:port, NULL if error.
 */
qdpn_connector_t *qdpn_connector_fd(qdpn_driver_t *driver, pn_socket_t fd, void *context);

/** Get the file descriptor for this connector */
int qdpn_connector_get_fd(qdpn_connector_t *connector);

/** Set the wakeup time on the connector */
void qdpn_connector_wakeup(qdpn_connector_t* c, pn_timestamp_t t);

/** Current time according */
pn_timestamp_t qdpn_now();

/** Implementation of connector methods (e.g. these are different for HTTP connectors */
typedef struct qdpn_connector_methods_t {
    void (*process)(qdpn_connector_t *c);
    void (*close)(qdpn_connector_t *c);
} qdpn_connector_methods_t;

/** Set new methods for a connector (e.g. because it is a HTTP connector) */
void qdpn_connector_set_methods(qdpn_connector_t *c, qdpn_connector_methods_t *methods);

/**@}*/

#endif /* driver.h */
