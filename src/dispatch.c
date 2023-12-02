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

#include "python_private.h"
#include "qpid/dispatch/python_embedded.h"

#include "qpid/dispatch.h"

#include "config.h"
#include "dispatch_private.h"
#include "entity.h"
#include "entity_cache.h"
#include "http.h"
#include "log_private.h"
#include "message_private.h"
#include "policy.h"
#include "router_private.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/discriminator.h"
#include "qpid/dispatch/server.h"
#include "qpid/dispatch/static_assert.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <stdlib.h>

/**
 * Private Function Prototypes
 */
qd_server_t    *qd_server(qd_dispatch_t *qd, int tc, const char *container_name,
                          const char *sasl_config_path, const char *sasl_config_name);
void            qd_server_free(qd_server_t *server);
qd_container_t *qd_container(qd_dispatch_t *qd);
void            qd_container_free(qd_container_t *container);
qd_policy_t    *qd_policy(qd_dispatch_t *qd);
void            qd_policy_free(qd_policy_t *policy);
qd_router_t    *qd_router(qd_dispatch_t *qd, qd_router_mode_t mode, const char *area, const char *id);
void            qd_router_setup_late(qd_dispatch_t *qd);
void            qd_router_free(qd_router_t *router);
void            qd_error_initialize();
static void qd_dispatch_set_router_id(qd_dispatch_t *qd, char *_id);
static void qd_dispatch_set_router_area(qd_dispatch_t *qd, char *_area);

const char     *CLOSEST_DISTRIBUTION   = "closest";
const char     *MULTICAST_DISTRIBUTION = "multicast";
const char     *BALANCED_DISTRIBUTION  = "balanced";
const char     *UNAVAILABLE_DISTRIBUTION = "unavailable";

sys_atomic_t global_delivery_id;

qd_dispatch_t *qd = 0;

qd_dispatch_t *qd_dispatch_get_dispatch()
{
    return qd;
}

qd_dispatch_t *qd_dispatch(const char *python_pkgdir, bool test_hooks)
{
    //
    // Seed the random number generator
    //
    struct timeval time;
    gettimeofday(&time, NULL);
    srandom((unsigned int)time.tv_sec + ((unsigned int)time.tv_usec << 11));
    
    qd = NEW(qd_dispatch_t);
    ZERO(qd);

    qd_entity_cache_initialize();   /* Must be first */
    qd_alloc_initialize();
    qd_log_initialize();
    qd_error_initialize();
    if (qd_error_code()) { qd_dispatch_free(qd); return 0; }

    if (python_pkgdir) {
        struct stat st;
        if (stat(python_pkgdir, &st)) {
            qd_error_errno(errno, "Cannot find Python library path '%s'", python_pkgdir);
            return NULL;
        } else if (!S_ISDIR(st.st_mode)) {
            qd_error(QD_ERROR_RUNTIME, "Python library path '%s' not a directory", python_pkgdir);
            return NULL;
        }
    }

    qd_dispatch_set_router_area(qd, strdup("0"));
    qd_dispatch_set_router_id(qd, strdup("0"));
    qd->router_mode = QD_ROUTER_MODE_ENDPOINT;
    qd->default_treatment   = QD_TREATMENT_LINK_BALANCED;
    qd->test_hooks          = test_hooks;

    qd_python_initialize(qd, python_pkgdir);
    if (qd_error_code()) { qd_dispatch_free(qd); return 0; }
    qd_message_initialize();
    if (qd_error_code()) { qd_dispatch_free(qd); return 0; }
    qd->dl_handle = 0;
    return qd;
}

qd_error_t qd_dispatch_load_config(qd_dispatch_t *qd, const char *config_path)
{
    // `dlopen(NULL, ...)` opens the current executable; qdrouterd used to dlopen libqpid-dispatch.so here before
    qd->dl_handle = dlopen(NULL, RTLD_LAZY | RTLD_NOLOAD);
    if (!qd->dl_handle)
        return qd_error(QD_ERROR_RUNTIME, "Failed to dlopen the current executable");

    qd_python_lock_state_t lock_state = qd_python_lock();
    PyObject *module = PyImport_ImportModule("qpid_dispatch_internal.management.config");
    PyObject *configure_dispatch = module ? PyObject_GetAttrString(module, "configure_dispatch") : NULL;
    Py_XDECREF(module);
    PyObject *result = configure_dispatch ? PyObject_CallFunction(configure_dispatch, "(NNs)", PyLong_FromVoidPtr(qd),
                                                                  PyLong_FromVoidPtr(qd->dl_handle), config_path)
                                          : NULL;
    Py_XDECREF(configure_dispatch);
    if (!result) qd_error_py();
    Py_XDECREF(result);
    qd_python_unlock(lock_state);
    return qd_error_code();
}

qd_error_t qd_dispatch_validate_config(const char *config_path)
{
	FILE* config_file = NULL;
	char config_data = '\0';
	qd_error_t validation_error = QD_ERROR_CONFIG;

	do {
		if (!config_path) {
			validation_error = qd_error(QD_ERROR_VALUE, "Configuration path value was empty");
			break;
		}

		config_file = fopen(config_path, "r");
		if (!config_file) {
			validation_error = qd_error(QD_ERROR_NOT_FOUND, "Configuration file could not be opened");
			break;
		}

		// TODO Check the actual minimum number of bytes required for the smallest valid configuration file
		if (!fread((void*)&config_data, 1, 1, config_file)) {
			validation_error = qd_error(QD_ERROR_CONFIG, "Configuration file was empty");
			break;
		}

		// TODO Add real validation code

		validation_error = QD_ERROR_NONE;
	} while (false); // do once

	if (config_file)
	{
		fclose(config_file);
	}

	return validation_error;
}

// Takes ownership of distribution string.
static void qd_dispatch_set_router_default_distribution(qd_dispatch_t *qd, char *distribution)
{
    if (distribution) {
        if (strcmp(distribution, MULTICAST_DISTRIBUTION) == 0)
            qd->default_treatment = QD_TREATMENT_MULTICAST_ONCE;
        else if (strcmp(distribution, CLOSEST_DISTRIBUTION) == 0)
            qd->default_treatment = QD_TREATMENT_ANYCAST_CLOSEST;
        else if (strcmp(distribution, BALANCED_DISTRIBUTION) == 0)
            qd->default_treatment = QD_TREATMENT_ANYCAST_BALANCED;
        else if (strcmp(distribution, UNAVAILABLE_DISTRIBUTION) == 0)
            qd->default_treatment = QD_TREATMENT_UNAVAILABLE;
    }
    else
        // The default for the router defaultDistribution field is QD_TREATMENT_ANYCAST_BALANCED
        qd->default_treatment = QD_TREATMENT_ANYCAST_BALANCED;
    free(distribution);
}

qd_error_t qd_dispatch_configure_router(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_dispatch_set_router_default_distribution(qd, qd_entity_opt_string(entity, "defaultDistribution", 0)); QD_ERROR_RET();
    qd_dispatch_set_router_id(qd, qd_entity_opt_string(entity, "id", 0)); QD_ERROR_RET();
    qd->router_mode = qd_entity_get_long(entity, "mode"); QD_ERROR_RET();
    if (!qd->router_id) {
        char *mode = 0;
        switch (qd->router_mode) {
        case QD_ROUTER_MODE_STANDALONE: mode = "Standalone_"; break;
        case QD_ROUTER_MODE_INTERIOR:   mode = "Interior_";   break;
        case QD_ROUTER_MODE_EDGE:       mode = "Edge_";       break;
        case QD_ROUTER_MODE_ENDPOINT:   mode = "Endpoint_";   break;
        }
        
        qd->router_id = (char*) malloc(strlen(mode) + QD_DISCRIMINATOR_SIZE + 2);
        strcpy(qd->router_id, mode);
        qd_generate_discriminator(qd->router_id + strlen(qd->router_id));
    }

    qd->thread_count = qd_entity_opt_long(entity, "workerThreads", 4); QD_ERROR_RET();
    qd->allow_resumable_link_route = qd_entity_opt_bool(entity, "allowResumableLinkRoute", true); QD_ERROR_RET();
    qd->timestamps_in_utc = qd_entity_opt_bool(entity, "timestampsInUTC", false); QD_ERROR_RET();
    qd->timestamp_format = qd_entity_opt_string(entity, "timestampFormat", 0); QD_ERROR_RET();
    qd->metadata = qd_entity_opt_string(entity, "metadata", 0); QD_ERROR_RET();

    if (! qd->sasl_config_path) {
        qd->sasl_config_path = qd_entity_opt_string(entity, "saslConfigDir", 0); QD_ERROR_RET();
    }
    if (! qd->sasl_config_name) {
        qd->sasl_config_name = qd_entity_opt_string(entity, "saslConfigName", "qdrouterd"); QD_ERROR_RET();
    }

    char *dump_file = qd_entity_opt_string(entity, "debugDumpFile", 0); QD_ERROR_RET();
    if (dump_file) {
        qd_alloc_debug_dump(dump_file); QD_ERROR_RET();
        free(dump_file);
    }

    return QD_ERROR_NONE;

}

qd_error_t qd_dispatch_configure_address(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_address(qd->router, entity);
    return qd_error_code();
}

QD_EXPORT qd_error_t qd_dispatch_configure_link_route(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_link_route(qd->router, entity);
    return qd_error_code();
}

QD_EXPORT qd_error_t qd_dispatch_configure_auto_link(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_auto_link(qd->router, entity);
    return qd_error_code();
}

QD_EXPORT qd_error_t qd_dispatch_configure_exchange(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_exchange(qd->router, entity);
    return qd_error_code();
}

QD_EXPORT qd_error_t qd_dispatch_configure_binding(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_binding(qd->router, entity);
    return qd_error_code();
}

qd_error_t qd_dispatch_configure_policy(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_error_t err;
    err = qd_entity_configure_policy(qd->policy, entity);
    if (err)
        return err;
    return QD_ERROR_NONE;
}


qd_error_t qd_dispatch_register_policy_manager(qd_dispatch_t *qd, qd_entity_t *entity)
{
    return qd_register_policy_manager(qd->policy, entity);
}


qd_error_t qd_dispatch_register_display_name_service(qd_dispatch_t *qd, void *object)
{
    return qd_register_display_name_service(qd, object);
}


QD_EXPORT long qd_dispatch_policy_c_counts_alloc()
{
    return qd_policy_c_counts_alloc();
}


QD_EXPORT void qd_dispatch_policy_c_counts_free(long ccounts)
{
    qd_policy_c_counts_free(ccounts);
}

QD_EXPORT void qd_dispatch_policy_c_counts_refresh(long ccounts, qd_entity_t *entity)
{
    qd_policy_c_counts_refresh(ccounts, entity);
}

QD_EXPORT bool qd_dispatch_policy_host_pattern_add(qd_dispatch_t *qd, void *py_obj)
{
    char *hostPattern = py_string_2_c(py_obj);
    bool rc = qd_policy_host_pattern_add(qd->policy, hostPattern);
    free(hostPattern);
    return rc;
}

QD_EXPORT void qd_dispatch_policy_host_pattern_remove(qd_dispatch_t *qd, void *py_obj)
{
    char *hostPattern = py_string_2_c(py_obj);
    qd_policy_host_pattern_remove(qd->policy, hostPattern);
    free(hostPattern);
}

QD_EXPORT char * qd_dispatch_policy_host_pattern_lookup(qd_dispatch_t *qd, void *py_obj)
{
    char *hostPattern = py_string_2_c(py_obj);
    char *rc = qd_policy_host_pattern_lookup(qd->policy, hostPattern);
    free(hostPattern);
    return rc;
}

qd_error_t qd_dispatch_prepare(qd_dispatch_t *qd)
{
    qd_log_global_options(qd->timestamp_format, qd->timestamps_in_utc);
    qd->server             = qd_server(qd, qd->thread_count, qd->router_id, qd->sasl_config_path, qd->sasl_config_name);
    qd->container          = qd_container(qd);
    qd->router             = qd_router(qd, qd->router_mode, qd->router_area, qd->router_id);
    qd->connection_manager = qd_connection_manager(qd);
    qd->policy             = qd_policy(qd);
    return qd_error_code();
}

void qd_dispatch_set_agent(qd_dispatch_t *qd, void *agent) {
    assert(agent);
    assert(!qd->agent);
    Py_IncRef(agent);  // TODO: why not incref here? actually probably needed, I get a leak in test 9 without this, strange...
    qd->agent = agent;
}

// Takes ownership of _id
static void qd_dispatch_set_router_id(qd_dispatch_t *qd, char *_id) {
    if (qd->router_id) {
        free(qd->router_id);
    }
    qd->router_id = _id;
}

// Takes ownership of _area
static void qd_dispatch_set_router_area(qd_dispatch_t *qd, char *_area) {
    if (qd->router_area) {
        free(qd->router_area);
    }
    qd->router_area = _area;
}

void qd_dispatch_free(qd_dispatch_t *qd)
{
    if (!qd) return;

    /* Stop HTTP threads immediately */
    qd_http_server_free(qd_server_http(qd->server));

    free(qd->sasl_config_path);
    free(qd->sasl_config_name);
    qd_connection_manager_free(qd->connection_manager);
    qd_policy_free(qd->policy);
    Py_XDECREF((PyObject*) qd->agent);
//    Py_XDECREF(qd_python_module());  // hack
    PyGC_Collect();  // run destructors while we still have the router around
//    int ret = PyRun_SimpleString("import objgraph; import gc; gc.collect(); from qpid_dispatch_internal.management import config");
//    assert(ret == 0);

//    qd_python_finalize();

    qd_router_free(qd->router);
    qd_container_free(qd->container);
    qd_server_free(qd->server);
    qd_log_finalize();
    qd_alloc_finalize();  // python objects may use alloc pool objects during finalization? TODO: they do that now  // actually, no, too late to do real work now
    qd_dispatch_set_router_id(qd, NULL);
    qd_dispatch_set_router_area(qd, NULL);
    qd_iterator_finalize();
    free(qd->timestamp_format);
    free(qd->metadata);

    free(qd);
}


QD_EXPORT void qd_dispatch_router_lock(qd_dispatch_t *qd) { sys_mutex_lock(qd->router->lock); }
QD_EXPORT void qd_dispatch_router_unlock(qd_dispatch_t *qd) { sys_mutex_unlock(qd->router->lock); }

qdr_core_t* qd_dispatch_router_core(qd_dispatch_t *qd) {
    return qd->router->router_core;
}


/* qd_router_memory_usage
 *
 * Return the amount of memory currently provisioned by the qdrouterd process.
 * This includes data, stack, and code memory.  On systems supporting virtual
 * memory this value may be larger than the physical RAM available on the
 * platform.
 *
 * Return 0 if the memory usage cannot be determined.
 */
uint64_t qd_router_memory_usage()
{
    // @TODO(kgiusti): only works for linux (what? doesn't everyone run linux?)

    // parse the VmSize value out of the /proc/[pid]/status file
    const pid_t my_pid = getpid();
    const char *status_template = "/proc/%ld/status";
    char status_path[64];
    if (snprintf(status_path, 64, status_template, (long int)my_pid) >= 64) {
        // huh, did not fit?  Should not happen
        return 0;
    }

    FILE *status_fp = fopen(status_path, "r");
    if (!status_fp) {
        // possible - if not on linux
        return 0;
    }

    // the format of the /proc/[pid]/status file is documented in the linux man
    // pages (man proc)
    size_t buflen = 0;
    char *buffer = 0;
    uint64_t my_mem_kb = 0;
    int scanned = 0;
    while (getline(&buffer, &buflen, status_fp) != -1) {
        scanned = sscanf(buffer, "VmSize: %"SCNu64, &my_mem_kb);
        if (scanned == 1)
            break;
    }
    free(buffer);

    fclose(status_fp);
    return (scanned == 1) ? my_mem_kb * 1024 : 0;
}
