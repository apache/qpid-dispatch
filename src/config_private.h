#ifndef __config_private_h__
#define __config_private_h__ 1
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

#include <qpid/dispatch/config.h>
#include "dispatch_private.h"

void qd_config_initialize(void);
void qd_config_finalize(void);
qd_config_t *qd_config(void);
void qd_config_read(qd_config_t *config, const char *filename);
void qd_config_extend(qd_config_t *config, const char *text);
void qd_config_free(qd_config_t *config);

#endif
