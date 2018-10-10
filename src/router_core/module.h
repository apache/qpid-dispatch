#ifndef qd_router_core_module
#define qd_router_core_module 1
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

#include "router_core_private.h"

/**
 * Callback to initialize a core module at core thread startup
 *
 * @param core Pointer to the core object
 * @param module_context [out] Returned module context
 */
typedef void (*qdrc_module_init_t) (qdr_core_t *core, void **module_context);


/**
 * Callback to finailize a core module at core thread shutdown
 *
 * @param module_context The context returned by the module during the on_init call
 */
typedef void (*qdrc_module_final_t) (void *module_context);


/**
 * Declaration of a core module
 *
 * A module must declare itself by invoking the QDR_CORE_MODULE_DECLARE macro in its body.
 *
 * @param name A null-terminated literal string naming the module
 * @param on_init Pointer to a function for module initialization, called at core thread startup
 * @param on_final Pointer to a function for module finalization, called at core thread shutdown
 */
#define QDR_CORE_MODULE_DECLARE(name,on_init,on_final)   \
    static void modstart() __attribute__((constructor)); \
    void modstart() { qdr_register_core_module(name, on_init, on_final); }
void qdr_register_core_module(const char *name, qdrc_module_init_t on_init, qdrc_module_final_t on_final);


#endif
