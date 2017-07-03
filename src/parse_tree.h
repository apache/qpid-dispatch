#ifndef PARSE_TREE_H
#define PARSE_TREE_H 1
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

#include <stdbool.h>

#include <qpid/dispatch/ctools.h>
#include "alloc.h"


typedef struct parse_node parse_node_t;

parse_node_t *parse_tree_new(char token_sep);
void parse_tree_free(parse_node_t *node);

// return false to stop searching

typedef bool parse_tree_visit_t(void *handle,
                                const char *pattern,
                                void *payload);

void parse_tree_find(parse_node_t *node, const char *value,
                     parse_tree_visit_t *callback, void *handle);

void *parse_tree_add_pattern(parse_node_t *node,
                             const char *pattern,
                             void *payload);

// returns the payload void *
void *parse_tree_remove_pattern(parse_node_t *node,
                                const char *pattern);
#endif /* parse_tree.h */
