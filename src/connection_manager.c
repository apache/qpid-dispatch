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

#include "qpid/dispatch/connection_manager.h"

#include "connection_manager_private.h"
#include "dispatch_private.h"
#include "entity.h"
#include "server_private.h"

#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/failoverlist.h"
#include "qpid/dispatch/threading.h"

#include <proton/listener.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

struct qd_config_ssl_profile_t {
    DEQ_LINKS(qd_config_ssl_profile_t);
    char        *name;
    char        *ssl_password;
    char        *ssl_trusted_certificate_db;
    char        *ssl_uid_format;
    char        *uid_name_mapping_file;
    char        *ssl_certificate_file;
    char        *ssl_private_key_file;
    char        *ssl_ciphers;
    char        *ssl_protocols;
};

DEQ_DECLARE(qd_config_ssl_profile_t, qd_config_ssl_profile_list_t);

struct qd_config_sasl_plugin_t {
    DEQ_LINKS(qd_config_sasl_plugin_t);
    char        *name;
    char        *auth_service;
    char        *hostname;
    char        *sasl_init_hostname;
    char        *auth_ssl_profile;
};

DEQ_DECLARE(qd_config_sasl_plugin_t, qd_config_sasl_plugin_list_t);

struct qd_connection_manager_t {
    qd_log_source_t              *log_source;
    qd_server_t                  *server;
    qd_listener_list_t            listeners;
    qd_connector_list_t           connectors;
    qd_config_ssl_profile_list_t  config_ssl_profiles;
    qd_config_sasl_plugin_list_t  config_sasl_plugins;
};

const char *qd_log_message_components[] =
    {"message-id",
     "user-id",
     "to",
     "subject",
     "reply-to",
     "correlation-id",
     "content-type",
     "content-encoding",
     "absolute-expiry-time",
     "creation-time",
     "group-id",
     "group-sequence",
     "reply-to-group-id",
     "app-properties",
     0};

const char *ALL = "all";
const char *NONE = "none";

/**
 * Search the list of config_ssl_profiles for an ssl-profile that matches the passed in name
 */
static qd_config_ssl_profile_t *qd_find_ssl_profile(qd_connection_manager_t *cm, char *name)
{
    qd_config_ssl_profile_t *ssl_profile = DEQ_HEAD(cm->config_ssl_profiles);
    while (ssl_profile) {
        if (strcmp(ssl_profile->name, name) == 0)
            return ssl_profile;
        ssl_profile = DEQ_NEXT(ssl_profile);
    }

    return 0;
}

/**
 * Read the file from the password_file location on the file system and populate password_field with the
 * contents of the file.
 */
static void qd_set_password_from_file(const char *password_file, char **password_field, qd_log_source_t *log_source)
{
    if (password_file) {
        FILE *file = fopen(password_file, "r");

        if (file == NULL) {
            //
            // The global variable errno (found in <errno.h>) contains information about what went wrong; you can use perror() to print that information as a readable string
            //
            qd_log(log_source, QD_LOG_ERROR, "Unable to open password file %s, error: %s", password_file, strerror(errno));
            return;
        }

        char buffer[200];

        int c;
        int i=0;

        while (i < 200 - 1) {
            c = fgetc(file);
            if (c == EOF || c == '\n')
                break;
            buffer[i++] = c;
        }

        if (i != 0) {
            buffer[i] = '\0';
            free(*password_field);
            *password_field = strdup(buffer);
        }
        fclose(file);
    }
}

/**
 * Search the list of config_sasl_plugins for an sasl-profile that matches the passed in name
 */
static qd_config_sasl_plugin_t *qd_find_sasl_plugin(qd_connection_manager_t *cm, char *name)
{
    qd_config_sasl_plugin_t *sasl_plugin = DEQ_HEAD(cm->config_sasl_plugins);
    while (sasl_plugin) {
        if (strcmp(sasl_plugin->name, name) == 0)
            return sasl_plugin;
        sasl_plugin = DEQ_NEXT(sasl_plugin);
    }

    return 0;
}

void qd_server_config_free(qd_server_config_t *cf)
{
    if (!cf) return;
    free(cf->host);
    free(cf->port);
    free(cf->host_port);
    free(cf->role);
    if (cf->http_root_dir)   free(cf->http_root_dir);
    if (cf->name)            free(cf->name);
    if (cf->protocol_family) free(cf->protocol_family);
    if (cf->sasl_username)   free(cf->sasl_username);
    if (cf->sasl_password)   free(cf->sasl_password);
    if (cf->sasl_mechanisms) free(cf->sasl_mechanisms);
    if (cf->ssl_profile)     free(cf->ssl_profile);
    if (cf->failover_list)   qd_failover_list_free(cf->failover_list);
    if (cf->log_message)     free(cf->log_message);

    if (cf->ssl_certificate_file)       free(cf->ssl_certificate_file);
    if (cf->ssl_private_key_file)       free(cf->ssl_private_key_file);
    if (cf->ssl_ciphers)                free(cf->ssl_ciphers);
    if (cf->ssl_protocols)              free(cf->ssl_protocols);
    if (cf->ssl_password)               free(cf->ssl_password);
    if (cf->ssl_trusted_certificate_db) free(cf->ssl_trusted_certificate_db);
    if (cf->ssl_uid_format)             free(cf->ssl_uid_format);
    if (cf->ssl_uid_name_mapping_file)  free(cf->ssl_uid_name_mapping_file);

    if (cf->sasl_plugin_config.auth_service)               free(cf->sasl_plugin_config.auth_service);
    if (cf->sasl_plugin_config.hostname)                   free(cf->sasl_plugin_config.hostname);
    if (cf->sasl_plugin_config.sasl_init_hostname)         free(cf->sasl_plugin_config.sasl_init_hostname);
    if (cf->sasl_plugin_config.ssl_certificate_file)       free(cf->sasl_plugin_config.ssl_certificate_file);
    if (cf->sasl_plugin_config.ssl_private_key_file)       free(cf->sasl_plugin_config.ssl_private_key_file);
    if (cf->sasl_plugin_config.ssl_ciphers)                free(cf->sasl_plugin_config.ssl_ciphers);
    if (cf->sasl_plugin_config.ssl_protocols)              free(cf->sasl_plugin_config.ssl_protocols);
    if (cf->sasl_plugin_config.ssl_password)               free(cf->sasl_plugin_config.ssl_password);
    if (cf->sasl_plugin_config.ssl_trusted_certificate_db) free(cf->sasl_plugin_config.ssl_trusted_certificate_db);
    if (cf->sasl_plugin_config.ssl_uid_format)             free(cf->sasl_plugin_config.ssl_uid_format);
    if (cf->sasl_plugin_config.ssl_uid_name_mapping_file)  free(cf->sasl_plugin_config.ssl_uid_name_mapping_file);

    memset(cf, 0, sizeof(*cf));
}

#define CHECK() if (qd_error_code()) goto error
#define SSTRDUP(S) ((S) ? strdup(S) : NULL)

/**
 * Private function to set the values of booleans strip_inbound_annotations and strip_outbound_annotations
 * based on the corresponding values for the settings in qdrouter.json
 * strip_inbound_annotations and strip_outbound_annotations are defaulted to true
 */
static void load_strip_annotations(qd_server_config_t *config, const char* stripAnnotations)
{
    if (stripAnnotations) {
    	if      (strcmp(stripAnnotations, "both") == 0) {
    		config->strip_inbound_annotations  = true;
    		config->strip_outbound_annotations = true;
    	}
    	else if (strcmp(stripAnnotations, "in") == 0) {
    		config->strip_inbound_annotations  = true;
    		config->strip_outbound_annotations = false;
    	}
    	else if (strcmp(stripAnnotations, "out") == 0) {
    		config->strip_inbound_annotations  = false;
    		config->strip_outbound_annotations = true;
    	}
    	else if (strcmp(stripAnnotations, "no") == 0) {
    		config->strip_inbound_annotations  = false;
    		config->strip_outbound_annotations = false;
    	}
    }
    else {
    	assert(stripAnnotations);
    	//This is just for safety. Default to stripInboundAnnotations and stripOutboundAnnotations to true (to "both").
		config->strip_inbound_annotations  = true;
		config->strip_outbound_annotations = true;
    }
}

/**
 * Since both the host and the addr have defaults of 127.0.0.1, we will have to use the non-default wherever it is available.
 */
static void set_config_host(qd_server_config_t *config, qd_entity_t* entity)
{
    config->host = qd_entity_opt_string(entity, "host", 0);

    assert(config->host);

    int hplen = strlen(config->host) + strlen(config->port) + 2;
    config->host_port = malloc(hplen);
    snprintf(config->host_port, hplen, "%s:%s", config->host, config->port);
}

static void qd_config_process_password(char **actual_val, char *pw, bool *is_file, bool allow_literal_prefix, qd_log_source_t *log_source)
{
    if (!pw)
        return;

    //
    // If the "password" starts with "env:" then the remaining
    // text is the environment variable that contains the password
    //
    if (strncmp(pw, "env:", 4) == 0) {
        char *env = pw + 4;
        // skip the leading whitespace if it is there
        while (*env == ' ') ++env;

        const char* passwd = getenv(env);
        if (passwd) {
            //
            // Replace the allocated directive with the looked-up password
            //
            *actual_val = strdup(passwd);
        } else {
            qd_error(QD_ERROR_NOT_FOUND, "Failed to find a password in the environment variable");
        }
    }

    //
    // If the "password" starts with "literal:" or "pass:" then
    // the remaining text is the password and the heading should be
    // stripped off
    //
    else if ( (strncmp(pw, "literal:", 8) == 0 && allow_literal_prefix) || strncmp(pw, "pass:", 5) == 0) {
        qd_log(log_source, QD_LOG_WARNING, "It is unsafe to provide plain text passwords in the config file");

        if (strncmp(pw, "l", 1) == 0) {
            // skip the "literal:" header
            pw += 8;
        }
        else {
            // skip the "pass:" header
            pw += 5;
        }

        //
        // Replace the password with a copy of the string after "literal: or "pass:"
        //
        char *copy = strdup(pw);
        *actual_val = copy;
    }
    //
    // If the password starts with a file: literal set the is_file to true.
    //
    else if (strncmp(pw, "file:", 5) == 0) {
        pw += 5;

        // Replace the password with a copy of the string after "file:"
        char *copy = strdup(pw);
        *actual_val = copy;
        *is_file = true;
    }
    else {
        //
        // THe password field does not have any prefixes. Use it as plain text
        //
        qd_log(log_source, QD_LOG_WARNING, "It is unsafe to provide plain text passwords in the config file");

    }
}

static qd_log_bits populate_log_message(const qd_server_config_t *config)
{
    //May have to copy this string since strtok modifies original string.
    char *log_message = config->log_message;

    int32_t ret_val = 0;

    if (!log_message || strcmp(log_message, NONE) == 0)
        return ret_val;

    //If log_message is set to 'all', turn on all bits.
    if (strcmp(log_message, ALL) == 0)
        return INT32_MAX;

    char *delim = ",";

    /* get the first token */
    char *token = strtok(log_message, delim);

    const char *component = 0;

    /* walk through other tokens */
    while( token != NULL ) {
       for (int i=0;; i++) {
           component = qd_log_message_components[i];
           if (component == 0)
               break;

           if (strcmp(component, token) == 0) {
                   ret_val |= 1 << i;
           }
       }
       token = strtok(NULL, delim);
    }

    return ret_val;
}


static qd_error_t load_server_config(qd_dispatch_t *qd, qd_server_config_t *config, qd_entity_t* entity, bool is_listener)
{
    qd_error_clear();

    bool authenticatePeer   = qd_entity_opt_bool(entity, "authenticatePeer",  false);    CHECK();
    bool verifyHostName     = qd_entity_opt_bool(entity, "verifyHostname",    true);     CHECK();
    bool requireEncryption  = qd_entity_opt_bool(entity, "requireEncryption", false);    CHECK();
    bool requireSsl         = qd_entity_opt_bool(entity, "requireSsl",        false);    CHECK();

    ZERO(config);
    config->log_message          = qd_entity_opt_string(entity, "messageLoggingComponents", 0);     CHECK();
    config->log_bits             = populate_log_message(config);
    config->port                 = qd_entity_get_string(entity, "port");              CHECK();
    config->name                 = qd_entity_opt_string(entity, "name", 0);           CHECK();
    config->role                 = qd_entity_get_string(entity, "role");              CHECK();
    config->inter_router_cost    = qd_entity_opt_long(entity, "cost", 1);             CHECK();
    config->protocol_family      = qd_entity_opt_string(entity, "protocolFamily", 0); CHECK();
    config->healthz              = qd_entity_opt_bool(entity, "healthz", true);       CHECK();
    config->metrics              = qd_entity_opt_bool(entity, "metrics", true);       CHECK();
    config->websockets           = qd_entity_opt_bool(entity, "websockets", true);    CHECK();
    config->http                 = qd_entity_opt_bool(entity, "http", false);         CHECK();
    config->http_root_dir        = qd_entity_opt_string(entity, "httpRootDir", false);   CHECK();
    config->http = config->http || config->http_root_dir; /* httpRoot implies http */
    config->max_frame_size       = qd_entity_get_long(entity, "maxFrameSize");        CHECK();
    config->max_sessions         = qd_entity_get_long(entity, "maxSessions");         CHECK();
    uint64_t ssn_frames          = qd_entity_opt_long(entity, "maxSessionFrames", 0); CHECK();
    config->idle_timeout_seconds = qd_entity_get_long(entity, "idleTimeoutSeconds");  CHECK();
    if (is_listener) {
        config->initial_handshake_timeout_seconds = qd_entity_get_long(entity, "initialHandshakeTimeoutSeconds");  CHECK();
    }
    config->sasl_username        = qd_entity_opt_string(entity, "saslUsername", 0);   CHECK();
    config->sasl_password        = qd_entity_opt_string(entity, "saslPassword", 0);   CHECK();
    config->sasl_mechanisms      = qd_entity_opt_string(entity, "saslMechanisms", 0); CHECK();
    config->ssl_profile          = qd_entity_opt_string(entity, "sslProfile", 0);     CHECK();
    config->sasl_plugin          = qd_entity_opt_string(entity, "saslPlugin", 0);     CHECK();
    config->link_capacity        = qd_entity_opt_long(entity, "linkCapacity", 0);     CHECK();
    config->multi_tenant         = qd_entity_opt_bool(entity, "multiTenant", false);  CHECK();
    config->policy_vhost         = qd_entity_opt_string(entity, "policyVhost", 0);    CHECK();
    config->conn_props           = qd_entity_opt_map(entity, "openProperties");       CHECK();

    char *unused                 = qd_entity_opt_string(entity, "trustedCertsFile", 0);
    if (unused) {
        qd_log(qd->connection_manager->log_source, QD_LOG_WARNING,
               "Configuration listener attribute 'trustedCertsFile' is not used. Specify sslProfile caCertFile instead.");
        free(unused);
    }

    set_config_host(config, entity);

    if (config->sasl_password) {
        //
        //Process the sasl password field and set the right values based on prefixes.
        //
        char *actual_pass = 0;
        bool is_file_path = 0;
        qd_config_process_password(&actual_pass, config->sasl_password, &is_file_path, false, qd->connection_manager->log_source);
        if (actual_pass) {
            if (is_file_path) {
                qd_set_password_from_file(actual_pass, &config->sasl_password, qd->connection_manager->log_source);
                free(actual_pass);
            }
            else {
                free(config->sasl_password);
                config->sasl_password = actual_pass;
            }
        }
    }

    //
    // Handle the defaults for various settings
    //
    if (config->link_capacity == 0)
        config->link_capacity = 250;

    if (config->max_sessions == 0 || config->max_sessions > 32768)
        // Proton disallows > 32768
        config->max_sessions = 32768;

    if (config->max_frame_size < QD_AMQP_MIN_MAX_FRAME_SIZE)
        // Silently promote the minimum max-frame-size
        // Proton will do this but the number is needed for the
        // incoming capacity calculation.
        config->max_frame_size = QD_AMQP_MIN_MAX_FRAME_SIZE;

    //
    // Given session frame count and max frame size, compute session incoming_capacity
    //   On 64-bit systems the capacity has no practial limit.
    //   On 32-bit systems the largest default capacity is half the process address space.
    //
    bool is_64bit = sizeof(size_t) == 8;
#define MAX_32BIT_CAPACITY ((size_t)(2147483647))
    if (ssn_frames == 0) {
        config->incoming_capacity = is_64bit ? MAX_32BIT_CAPACITY * (size_t)config->max_frame_size : MAX_32BIT_CAPACITY;
    } else {
        // Limited incoming frames.
        if (is_64bit) {
            // Specify this to proton by setting capacity to be
            // the product (max_frame_size * ssn_frames).
            config->incoming_capacity = (size_t)config->max_frame_size * (size_t)ssn_frames;
        } else {
            // 32-bit systems have an upper bound to the capacity
            uint64_t max_32bit_capacity = (uint64_t)MAX_32BIT_CAPACITY;
            uint64_t capacity     = (uint64_t)config->max_frame_size * (uint64_t)ssn_frames;
            if (capacity <= max_32bit_capacity) {
                config->incoming_capacity = (size_t)capacity;
            } else {
                config->incoming_capacity = MAX_32BIT_CAPACITY;
                uint64_t actual_frames = max_32bit_capacity / (uint64_t)config->max_frame_size;

                qd_log(qd->connection_manager->log_source, QD_LOG_WARNING,
                    "Server configuation for I/O adapter entity name:'%s', host:'%s', port:'%s', "
                    "requested maxSessionFrames truncated from %"PRId64" to %"PRId64,
                    config->name, config->host, config->port, ssn_frames, actual_frames);
            }
        }
    }

    //
    // For now we are hardwiring this attribute to true.  If there's an outcry from the
    // user community, we can revisit this later.
    //
    config->allowInsecureAuthentication = true;
    config->verify_host_name = verifyHostName;

    char *stripAnnotations  = qd_entity_opt_string(entity, "stripAnnotations", 0);
    load_strip_annotations(config, stripAnnotations);
    free(stripAnnotations);
    stripAnnotations = 0;
    CHECK();

    config->requireAuthentication = authenticatePeer;
    config->requireEncryption     = requireEncryption || requireSsl;

    if (config->ssl_profile) {
        config->ssl_required = requireSsl;
        config->ssl_require_peer_authentication = config->sasl_mechanisms &&
            strstr(config->sasl_mechanisms, "EXTERNAL") != 0;

        qd_config_ssl_profile_t *ssl_profile =
            qd_find_ssl_profile(qd->connection_manager, config->ssl_profile);
        if (ssl_profile) {
            config->ssl_certificate_file = SSTRDUP(ssl_profile->ssl_certificate_file);
            config->ssl_private_key_file = SSTRDUP(ssl_profile->ssl_private_key_file);
            config->ssl_ciphers = SSTRDUP(ssl_profile->ssl_ciphers);
            config->ssl_protocols = SSTRDUP(ssl_profile->ssl_protocols);
            config->ssl_password = SSTRDUP(ssl_profile->ssl_password);
            config->ssl_trusted_certificate_db = SSTRDUP(ssl_profile->ssl_trusted_certificate_db);
            config->ssl_uid_format = SSTRDUP(ssl_profile->ssl_uid_format);
            config->ssl_uid_name_mapping_file = SSTRDUP(ssl_profile->uid_name_mapping_file);
        }
    }

    if (config->sasl_plugin) {
        qd_config_sasl_plugin_t *sasl_plugin =
            qd_find_sasl_plugin(qd->connection_manager, config->sasl_plugin);
        if (sasl_plugin) {
            config->sasl_plugin_config.auth_service = SSTRDUP(sasl_plugin->auth_service);
            config->sasl_plugin_config.hostname = SSTRDUP(sasl_plugin->hostname);
            config->sasl_plugin_config.sasl_init_hostname = SSTRDUP(sasl_plugin->sasl_init_hostname);
            qd_log(qd->connection_manager->log_source, QD_LOG_INFO, "Using auth service %s from  SASL Plugin %s", config->sasl_plugin_config.auth_service, config->sasl_plugin);

            if (sasl_plugin->auth_ssl_profile) {
                config->sasl_plugin_config.use_ssl = true;
                qd_config_ssl_profile_t *auth_ssl_profile =
                    qd_find_ssl_profile(qd->connection_manager, sasl_plugin->auth_ssl_profile);

                config->sasl_plugin_config.ssl_certificate_file = SSTRDUP(auth_ssl_profile->ssl_certificate_file);
                config->sasl_plugin_config.ssl_private_key_file = SSTRDUP(auth_ssl_profile->ssl_private_key_file);
                config->sasl_plugin_config.ssl_ciphers = SSTRDUP(auth_ssl_profile->ssl_ciphers);
                config->sasl_plugin_config.ssl_protocols = SSTRDUP(auth_ssl_profile->ssl_protocols);
                config->sasl_plugin_config.ssl_password = SSTRDUP(auth_ssl_profile->ssl_password);
                config->sasl_plugin_config.ssl_trusted_certificate_db = SSTRDUP(auth_ssl_profile->ssl_trusted_certificate_db);
                config->sasl_plugin_config.ssl_uid_format = SSTRDUP(auth_ssl_profile->ssl_uid_format);
                config->sasl_plugin_config.ssl_uid_name_mapping_file = SSTRDUP(auth_ssl_profile->uid_name_mapping_file);
            } else {
                config->sasl_plugin_config.use_ssl = false;
            }
        } else {
            qd_error(QD_ERROR_RUNTIME, "Cannot find sasl plugin %s", config->sasl_plugin); CHECK();
        }
    }

    return QD_ERROR_NONE;

  error:
    qd_server_config_free(config);
    return qd_error_code();
}


bool is_log_component_enabled(qd_log_bits log_message, const char *component_name) {

    for(int i=0;;i++) {
        const char *component = qd_log_message_components[i];
        if (component == 0)
            break;
        if (strcmp(component_name, component) == 0)
            return (log_message >> i) & 1;
    }

    return 0;
}


static bool config_ssl_profile_free(qd_connection_manager_t *cm, qd_config_ssl_profile_t *ssl_profile)
{
    DEQ_REMOVE(cm->config_ssl_profiles, ssl_profile);

    free(ssl_profile->name);
    free(ssl_profile->ssl_password);
    free(ssl_profile->ssl_trusted_certificate_db);
    free(ssl_profile->ssl_uid_format);
    free(ssl_profile->uid_name_mapping_file);
    free(ssl_profile->ssl_certificate_file);
    free(ssl_profile->ssl_private_key_file);
    free(ssl_profile->ssl_ciphers);
    free(ssl_profile->ssl_protocols);
    free(ssl_profile);
    return true;

}

static bool config_sasl_plugin_free(qd_connection_manager_t *cm, qd_config_sasl_plugin_t *sasl_plugin)
{
    DEQ_REMOVE(cm->config_sasl_plugins, sasl_plugin);

    free(sasl_plugin->name);
    free(sasl_plugin->auth_service);
    free(sasl_plugin->hostname);
    free(sasl_plugin->sasl_init_hostname);
    free(sasl_plugin->auth_ssl_profile);
    free(sasl_plugin);
    return true;

}


QD_EXPORT qd_config_ssl_profile_t *qd_dispatch_configure_ssl_profile(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_error_clear();
    qd_connection_manager_t *cm = qd->connection_manager;

    qd_config_ssl_profile_t *ssl_profile = NEW(qd_config_ssl_profile_t);
    ZERO(ssl_profile);
    DEQ_ITEM_INIT(ssl_profile);
    DEQ_INSERT_TAIL(cm->config_ssl_profiles, ssl_profile);
    ssl_profile->name                       = qd_entity_opt_string(entity, "name", 0); CHECK();
    ssl_profile->ssl_certificate_file       = qd_entity_opt_string(entity, "certFile", 0); CHECK();
    ssl_profile->ssl_private_key_file       = qd_entity_opt_string(entity, "privateKeyFile", 0); CHECK();
    ssl_profile->ssl_password               = qd_entity_opt_string(entity, "password", 0); CHECK();
    char *password_file                     = qd_entity_opt_string(entity, "passwordFile", 0); CHECK();

    if (ssl_profile->ssl_password) {
        //
        // Process the password to handle any modifications or lookups needed
        //
        char *actual_pass = 0;
        bool is_file_path = 0;
        qd_config_process_password(&actual_pass, ssl_profile->ssl_password, &is_file_path, true, cm->log_source); CHECK();
        if (actual_pass) {
            if (is_file_path) {
                qd_set_password_from_file(actual_pass, &ssl_profile->ssl_password, cm->log_source);
                free(actual_pass);
            }
            else {
                free(ssl_profile->ssl_password);
                ssl_profile->ssl_password = actual_pass;
            }
        }
    }
    else if (password_file) {
        //
        // Warn the user that the passwordFile attribute has been deprecated.
        //
        qd_log(cm->log_source, QD_LOG_WARNING, "Attribute passwordFile of entity sslProfile has been deprecated. Use password field with the file: prefix instead.");
        qd_set_password_from_file(password_file, &ssl_profile->ssl_password, cm->log_source);
    }

    free(password_file);

    ssl_profile->ssl_ciphers   = qd_entity_opt_string(entity, "ciphers", 0);                   CHECK();
    ssl_profile->ssl_protocols = qd_entity_opt_string(entity, "protocols", 0);                 CHECK();
    ssl_profile->ssl_trusted_certificate_db = qd_entity_opt_string(entity, "caCertFile", 0);   CHECK();
    ssl_profile->ssl_uid_format             = qd_entity_opt_string(entity, "uidFormat", 0);          CHECK();
    ssl_profile->uid_name_mapping_file      = qd_entity_opt_string(entity, "uidNameMappingFile", 0); CHECK();

    qd_log(cm->log_source, QD_LOG_INFO, "Created SSL Profile with name %s ", ssl_profile->name);
    return ssl_profile;

    error:
        qd_log(cm->log_source, QD_LOG_ERROR, "Unable to create ssl profile: %s", qd_error_message());
        config_ssl_profile_free(cm, ssl_profile);
        return 0;
}

QD_EXPORT qd_config_sasl_plugin_t *qd_dispatch_configure_sasl_plugin(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_error_clear();
    qd_connection_manager_t *cm = qd->connection_manager;

    qd_config_sasl_plugin_t *sasl_plugin = NEW(qd_config_sasl_plugin_t);
    ZERO(sasl_plugin);
    DEQ_ITEM_INIT(sasl_plugin);
    DEQ_INSERT_TAIL(cm->config_sasl_plugins, sasl_plugin);
    sasl_plugin->name                       = qd_entity_opt_string(entity, "name", 0); CHECK();

    sasl_plugin->hostname = qd_entity_opt_string(entity, "host", 0);
    char *auth_port = qd_entity_opt_string(entity, "port", 0);

    if (sasl_plugin->hostname && auth_port) {
        int strlen_auth_host = strlen(sasl_plugin->hostname);
        int strlen_auth_port = strlen(auth_port);

        if (strlen_auth_host > 0 && strlen_auth_port > 0) {

            int hplen = strlen_auth_host + strlen_auth_port + 2;
            if (hplen > 2) {
                sasl_plugin->auth_service = malloc(hplen);
                snprintf(sasl_plugin->auth_service, hplen, "%s:%s", sasl_plugin->hostname, auth_port);
            }
        }
    }

    free(auth_port);

    if (!sasl_plugin->auth_service) {
        sasl_plugin->auth_service               = qd_entity_opt_string(entity, "authService", 0); CHECK();
        qd_log(cm->log_source, QD_LOG_WARNING, "Attribute authService of entity authServicePlugin has been deprecated. Use host and port instead.");
    }

    sasl_plugin->sasl_init_hostname         = qd_entity_opt_string(entity, "realm", 0); CHECK();
    sasl_plugin->auth_ssl_profile           = qd_entity_opt_string(entity, "sslProfile", 0); CHECK();

    qd_log(cm->log_source, QD_LOG_INFO, "Created SASL plugin config with name %s", sasl_plugin->name);
    return sasl_plugin;

    error:
        qd_log(cm->log_source, QD_LOG_ERROR, "Unable to create SASL plugin config: %s", qd_error_message());
        config_sasl_plugin_free(cm, sasl_plugin);
        return 0;
}

static void log_config(qd_log_source_t *log, qd_server_config_t *c, const char *what) {
    qd_log(log, QD_LOG_INFO, "Configured %s: %s proto=%s, role=%s%s%s%s",
           what, c->host_port, c->protocol_family ? c->protocol_family : "any",
           c->role,
           c->http ? ", http" : "",
           c->ssl_profile ? ", sslProfile=":"",
           c->ssl_profile ? c->ssl_profile:"");
}


QD_EXPORT qd_listener_t *qd_dispatch_configure_listener(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_connection_manager_t *cm = qd->connection_manager;
    qd_listener_t *li = qd_server_listener(qd->server);
    if (!li || load_server_config(qd, &li->config, entity, true) != QD_ERROR_NONE) {
        qd_log(cm->log_source, QD_LOG_ERROR, "Unable to create listener: %s", qd_error_message());
        qd_listener_decref(li);
        return 0;
    }
    char *fol = qd_entity_opt_string(entity, "failoverUrls", 0);
    if (fol) {
        li->config.failover_list = qd_failover_list(fol);
        free(fol);
        if (li->config.failover_list == 0) {
            qd_log(cm->log_source, QD_LOG_ERROR, "Unable to create listener, bad failover list: %s",
                   qd_error_message());
            qd_listener_decref(li);
            return 0;
        }
    } else {
        li->config.failover_list = 0;
    }
    DEQ_ITEM_INIT(li);
    DEQ_INSERT_TAIL(cm->listeners, li);
    log_config(cm->log_source, &li->config, "Listener");
    return li;
}


QD_EXPORT qd_error_t qd_entity_refresh_listener(qd_entity_t* entity, void *impl)
{
    return QD_ERROR_NONE;
}


/**
 * Calculates the total length of the failover  list string.
 * For example, the failover list string can look like this - "amqp://0.0.0.0:62616, amqp://0.0.0.0:61616"
 * This function calculates the length of the above string by adding up the scheme (amqp or amqps) and host_port for each failover item.
 * It also assumes that there will be a comma and a space between each failover item.
 *
 */
static int get_failover_info_length(qd_failover_item_list_t   conn_info_list)
{
    int arr_length = 0;
    qd_failover_item_t *item = DEQ_HEAD(conn_info_list);

    while(item) {
        if (item->scheme) {
            // The +3 is for the '://'
            arr_length += strlen(item->scheme) + 3;
        }
        if (item->host_port) {
            arr_length += strlen(item->host_port);
        }
        item = DEQ_NEXT(item);
        if (item) {
            // This is for the comma and space between the items
            arr_length += 2;
        }
    }

    if (arr_length > 0)
        // This is for the final '\0'
        arr_length += 1;

    return arr_length;
}

/**
 *
 * Creates a failover url list. This comma separated failover list shows a list of urls that the router will attempt
 * to connect to in case the primary connection fails. The router will attempt these failover connections to urls in
 * the order that they appear in the list.
 *
 */
qd_error_t qd_entity_refresh_connector(qd_entity_t* entity, void *impl)
{
    qd_connector_t *connector = (qd_connector_t*) impl;

    int conn_index = connector->conn_index;

    int i = 1;
    int num_items = 0;

    sys_mutex_lock(connector->lock);

    qd_failover_item_list_t   conn_info_list = connector->conn_info_list;

    int conn_info_len = DEQ_SIZE(conn_info_list);

    qd_failover_item_t *item = DEQ_HEAD(conn_info_list);

    int arr_length = get_failover_info_length(conn_info_list);

    // This is the string that will contain the comma separated failover list
    char *failover_info = qd_calloc(arr_length + 1, sizeof(char));
    while(item) {

        // Break out of the loop when we have hit all items in the list.
        if (num_items >= conn_info_len)
            break;

        if (num_items >= 1) {
            strcat(failover_info, ", ");
        }

        // We need to go to the elements in the list to get to the
        // element that matches the connection index. This is the first
        // url that the router will try to connect on failover.
        if (conn_index == i) {
            num_items += 1;
            if (item->scheme) {
                strcat(failover_info, item->scheme);
                strcat(failover_info, "://");
            }
            if (item->host_port) {
                strcat(failover_info, item->host_port);
            }
        }
        else {
            if (num_items > 0) {
                num_items += 1;
                if (item->scheme) {
                    strcat(failover_info, item->scheme);
                    strcat(failover_info, "://");
                }
                if (item->host_port) {
                    strcat(failover_info, item->host_port);
                }
            }
        }

        i += 1;

        item = DEQ_NEXT(item);
        if (item == 0)
            item = DEQ_HEAD(conn_info_list);
    }

    const char *state_info = 0;
    switch (connector->state) {
    case CXTR_STATE_CONNECTING:
      state_info = "CONNECTING";
      break;
    case CXTR_STATE_OPEN:
      state_info = "SUCCESS";
      break;
    case CXTR_STATE_FAILED:
      state_info = "FAILED";
      break;
    case CXTR_STATE_INIT:
      state_info = "INITIALIZING";
      break;
    case CXTR_STATE_DELETED:
      // deleted by management, waiting for connection to close
      state_info = "CLOSING";
      break;
    default:
      state_info = "UNKNOWN";
      break;
    }

    if (qd_entity_set_string(entity, "failoverUrls", failover_info) == 0
        && qd_entity_set_string(entity, "connectionStatus", state_info) == 0
        && qd_entity_set_string(entity, "connectionMsg", connector->conn_msg) == 0) {

        sys_mutex_unlock(connector->lock);
        free(failover_info);
        return QD_ERROR_NONE;
    }

    sys_mutex_unlock(connector->lock);
    free(failover_info);
    return qd_error_code();
}


QD_EXPORT qd_connector_t *qd_dispatch_configure_connector(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_connection_manager_t *cm = qd->connection_manager;
    qd_connector_t *ct = qd_server_connector(qd->server);

    qd_error_clear();

    if (ct && load_server_config(qd, &ct->config, entity, false) == QD_ERROR_NONE) {
        ct->policy_vhost = qd_entity_opt_string(entity, "policyVhost", 0); CHECK();
        DEQ_ITEM_INIT(ct);
        DEQ_INSERT_TAIL(cm->connectors, ct);
        log_config(cm->log_source, &ct->config, "Connector");

        //
        // Add the first item to the ct->conn_info_list
        // The initial connection information and any backup connection information is stored in the conn_info_list
        //
        qd_failover_item_t *item = NEW(qd_failover_item_t);
        ZERO(item);
        if (ct->config.ssl_required)
            item->scheme   = strdup("amqps");
        else
            item->scheme   = strdup("amqp");

        item->host     = strdup(ct->config.host);
        item->port     = strdup(ct->config.port);

        int hplen = strlen(item->host) + strlen(item->port) + 2;
        item->host_port = malloc(hplen);
        snprintf(item->host_port, hplen, "%s:%s", item->host , item->port);

        DEQ_INSERT_TAIL(ct->conn_info_list, item);

        return ct;
    }

  error:
    qd_log(cm->log_source, QD_LOG_ERROR, "Unable to create connector: %s", qd_error_message());
    qd_connector_decref(ct);
    return 0;
}


qd_connection_manager_t *qd_connection_manager(qd_dispatch_t *qd)
{
    qd_connection_manager_t *cm = NEW(qd_connection_manager_t);
    if (!cm)
        return 0;

    cm->log_source = qd_log_source("CONN_MGR");
    cm->server     = qd->server;
    DEQ_INIT(cm->listeners);
    DEQ_INIT(cm->connectors);
    DEQ_INIT(cm->config_ssl_profiles);
    DEQ_INIT(cm->config_sasl_plugins);

    return cm;
}


// Called on router shutdown
//
void qd_connection_manager_free(qd_connection_manager_t *cm)
{
    if (!cm) return;
    qd_listener_t *li = DEQ_HEAD(cm->listeners);
    while (li) {
        DEQ_REMOVE_HEAD(cm->listeners);
        if (li->pn_listener) {
            // DISPATCH-1508: force cleanup of pn_listener context.  This is
            // usually done in the PN_LISTENER_CLOSE event handler in server.c,
            // but since the router is going down those events will no longer
            // be generated.
            pn_listener_set_context(li->pn_listener, 0);
            pn_listener_close(li->pn_listener);
            li->pn_listener = 0;
            qd_listener_decref(li);  // for the pn_listener's context
        }
        qd_listener_decref(li);
        li = DEQ_HEAD(cm->listeners);
    }

    qd_connector_t *connector = DEQ_HEAD(cm->connectors);
    while (connector) {
        DEQ_REMOVE_HEAD(cm->connectors);
        sys_mutex_lock(connector->lock);
        // setting DELETED below ensures the timer callback
        // will not initiate a re-connect once we drop
        // the lock
        connector->state = CXTR_STATE_DELETED;
        sys_mutex_unlock(connector->lock);
        // cannot cancel timer while holding lock since the
        // callback takes the lock
        qd_timer_cancel(connector->timer);
        qd_connector_decref(connector);

        connector = DEQ_HEAD(cm->connectors);
    }

    qd_config_ssl_profile_t *sslp = DEQ_HEAD(cm->config_ssl_profiles);
    while (sslp) {
        config_ssl_profile_free(cm, sslp);
        sslp = DEQ_HEAD(cm->config_ssl_profiles);
    }

    qd_config_sasl_plugin_t *saslp = DEQ_HEAD(cm->config_sasl_plugins);
    while (saslp) {
        config_sasl_plugin_free(cm, saslp);
        saslp = DEQ_HEAD(cm->config_sasl_plugins);
    }

    free(cm);
}


/** NOTE: non-static qd_connection_manager_* functions are called from the python agent */


void qd_connection_manager_start(qd_dispatch_t *qd)
{
    static bool first_start = true;
    qd_listener_t  *li = DEQ_HEAD(qd->connection_manager->listeners);
    qd_connector_t *ct = DEQ_HEAD(qd->connection_manager->connectors);

    while (li) {
        if (!li->pn_listener) {
            if (!qd_listener_listen(li) && first_start) {
                qd_log(qd->connection_manager->log_source, QD_LOG_CRITICAL,
                       "Listen on %s failed during initial config", li->config.host_port);
                exit(1);
            } else {
                li->exit_on_error = first_start;
            }
        }
        li = DEQ_NEXT(li);
    }

    while (ct) {

        if (ct->state == CXTR_STATE_OPEN || ct->state == CXTR_STATE_CONNECTING) {
            ct = DEQ_NEXT(ct);
            continue;
        }

        qd_connector_connect(ct);
        ct = DEQ_NEXT(ct);
    }

    first_start = false;
}


QD_EXPORT void qd_connection_manager_delete_listener(qd_dispatch_t *qd, void *impl)
{
    qd_listener_t *li = (qd_listener_t*) impl;
    if (li) {
        if (li->pn_listener) {
            pn_listener_close(li->pn_listener);
        }
        else if (li->http) {
            qd_lws_listener_close(li->http);
        }
        DEQ_REMOVE(qd->connection_manager->listeners, li);
        qd_listener_decref(li);
    }
}


QD_EXPORT void qd_connection_manager_delete_ssl_profile(qd_dispatch_t *qd, void *impl)
{
    qd_config_ssl_profile_t *ssl_profile = (qd_config_ssl_profile_t*) impl;
    config_ssl_profile_free(qd->connection_manager, ssl_profile);
}

QD_EXPORT void qd_connection_manager_delete_sasl_plugin(qd_dispatch_t *qd, void *impl)
{
    qd_config_sasl_plugin_t *sasl_plugin = (qd_config_sasl_plugin_t*) impl;
    config_sasl_plugin_free(qd->connection_manager, sasl_plugin);
}


static void deferred_close(void *context, bool discard) {
    if (!discard) {
        pn_connection_close((pn_connection_t*)context);
    }
}


// threading: called by management thread while I/O thread may be
// referencing the qd_connector_t via the qd_connection_t
//
QD_EXPORT void qd_connection_manager_delete_connector(qd_dispatch_t *qd, void *impl)
{
    qd_connector_t *ct = (qd_connector_t*) impl;
    if (ct) {
        // cannot free the timer while holding ct->lock since the
        // timer callback may be running during the call to qd_timer_free
        qd_timer_t *timer = 0;
        void *dct = qd_connection_new_qd_deferred_call_t();
        sys_mutex_lock(ct->lock);
        timer = ct->timer;
        ct->timer = 0;
        ct->state = CXTR_STATE_DELETED;
        qd_connection_t *conn = ct->qd_conn;
        if (conn && conn->pn_conn) {
            qd_connection_invoke_deferred_impl(conn, deferred_close, conn->pn_conn, dct);
            sys_mutex_unlock(ct->lock);
        } else {
            sys_mutex_unlock(ct->lock);
            qd_connection_free_qd_deferred_call_t(dct);
        }
        qd_timer_free(timer);
        DEQ_REMOVE(qd->connection_manager->connectors, ct);
        qd_connector_decref(ct);
    }
}


const char *qd_connector_name(qd_connector_t *ct)
{
    return ct ? ct->config.name : 0;
}

