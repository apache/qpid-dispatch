#ifndef __io_module_h__
#define __io_module_h__ 1
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

#include "qpid/dispatch/router_core.h"

/**
 ******************************************************************************
 * Protocol adaptor declaration macro
 ******************************************************************************
 */
/**
 * Callback to initialize a protocol adaptor at core thread startup
 *
 * @param core Pointer to the core object
 * @param adaptor_context [out] Returned adaptor context
 */
typedef void (*qdr_adaptor_init_t) (qdr_core_t *core, void **adaptor_context);


/**
 * Callback to finalize a protocol adaptor at core thread shutdown
 *
 * @param adaptor_context The context returned by the adaptor during the on_init call
 */
typedef void (*qdr_adaptor_final_t) (void *adaptor_context);


/**
 * Declaration of a protocol adaptor
 *
 * A protocol adaptor may declare itself by invoking the QDR_CORE_ADAPTOR_DECLARE macro in its body.
 *
 * @param name A null-terminated literal string naming the module
 * @param on_init Pointer to a function for adaptor initialization, called at core thread startup
 * @param on_final Pointer to a function for adaptor finalization, called at core thread shutdown
 */
#define QDR_CORE_ADAPTOR_DECLARE_ORD(name,on_init,on_final,ord)      \
    static void adaptorstart() __attribute__((constructor)); \
    void adaptorstart() { qdr_register_adaptor(name, on_init, on_final, ord); }
#define QDR_CORE_ADAPTOR_DECLARE(name,on_init,on_final) QDR_CORE_ADAPTOR_DECLARE_ORD(name,on_init,on_final,100)
void qdr_register_adaptor(const char         *name,
                          qdr_adaptor_init_t  on_init,
                          qdr_adaptor_final_t on_final,
                          uint32_t            ordinal);


#endif
