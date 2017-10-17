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
#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/alloc.h>


typedef struct qd_parse_node qd_parse_tree_t;
extern const char * const QD_PARSE_TREE_TOKEN_SEP;

qd_parse_tree_t *qd_parse_tree_new(void);
void qd_parse_tree_free(qd_parse_tree_t *tree);


// returns old payload or NULL if new
void *qd_parse_tree_add_pattern(qd_parse_tree_t *node,
                                const qd_iterator_t *pattern,
                                void *payload);

// returns old payload or NULL if not present
void *qd_parse_tree_remove_pattern(qd_parse_tree_t *node,
                                   const qd_iterator_t *pattern);

// retrieves the payload pointer
// returns true if pattern found
bool qd_parse_tree_get_pattern(qd_parse_tree_t *tree,
                               const qd_iterator_t *pattern,
                               void **payload);

// find the 'best' match to 'value', using the following precedence (highest
// first):
//
// 1) exact token match
// 2) * wildcard match
// 3) # wildcard match
//
// example:
//   given patterns
//   1) 'a.b.c'
//   2) 'a.b.*'
//   3)'a.b.#'
//
//  'a.b.c' will match 1
//  'a.b.x' will match 2
//  'a.b' and 'a.b.c.x' will match 3
//
// returns true on match and sets *payload
bool qd_parse_tree_retrieve_match(qd_parse_tree_t *tree,
                                  const qd_iterator_t *value,
                                  void **payload);

// parse tree traversal

// return false to stop tree transversal
typedef bool qd_parse_tree_visit_t(void *handle,
                                   const char *pattern,
                                   void *payload);

// visit each matching pattern that matches value in the order based on the
// above precedence rules
void qd_parse_tree_search(qd_parse_tree_t *tree, const qd_iterator_t *value,
                          qd_parse_tree_visit_t *callback, void *handle);

// visit each terminal node on the tree, returns last value returned by callback
bool qd_parse_tree_walk(qd_parse_tree_t *tree, qd_parse_tree_visit_t *callback, void *handle);

#endif /* parse_tree.h */
