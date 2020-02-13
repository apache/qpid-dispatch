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


#include "parse_tree.h"
#include <qpid/dispatch/log.h>


// token parsing
// parse a string of tokens separated by a boundary character.
//
// The format of a pattern string is a series of tokens. A token can contain
// any characters but those reserved for wildcards or the boundary character.
//
// TODO(kgiusti): really should create a token parsing qd_iterator_t view.
// Then all this token/token_iterator stuff can be removed...
//
typedef struct token {
    const char *begin;
    const char *end;
} token_t;
#define TOKEN_LEN(t) ((t).end - (t).begin)

// for iterating over a string of tokens:
typedef struct token_iterator {
    char match_1;         // match any 1 token
    char match_glob;      // match 0 or more tokens
    const char *separators;
    token_t token;           // token at head of string
    const char *terminator;  // end of entire string
} token_iterator_t;


static void token_iterator_init(token_iterator_t *t,
                                qd_parse_tree_type_t type,
                                const char *str)
{
    switch (type) {
    default:  // See DISPATCH-1567: shuts up some bogus compiler warnings
        assert(false);
        // fall through
    case QD_PARSE_TREE_ADDRESS:
        t->separators = "./";   // accept either for backward compatibility
        t->match_1 = '*';
        t->match_glob = '#';
        break;
    case QD_PARSE_TREE_AMQP_0_10:
        t->separators = ".";
        t->match_1 = '*';
        t->match_glob = '#';
        break;
    case QD_PARSE_TREE_MQTT:
        t->separators = "/";
        t->match_1 = '+';
        t->match_glob = '#';
        break;
    }

    while (*str && strchr(t->separators, *str))
        str++;  // skip any leading separators
    const char *tend = strpbrk(str, t->separators);
    t->terminator = str + strlen(str);
    t->token.begin = str;
    t->token.end = (tend) ? tend : t->terminator;
}


static void token_iterator_next(token_iterator_t *t)
{
    if (t->token.end == t->terminator) {
        t->token.begin = t->terminator;
    } else {
        const char *tend;
        t->token.begin = t->token.end + 1;
        tend = strpbrk(t->token.begin, t->separators);
        t->token.end = (tend) ? tend : t->terminator;
    }
}


const char address_token_sep[] = "./";
const char *qd_parse_address_token_sep() {
    return address_token_sep;
}

static bool token_iterator_done(const token_iterator_t *t)
{
    return t->token.begin == t->terminator;
}


static void token_iterator_pop(token_iterator_t *t, token_t *head)
{
    if (head) *head = t->token;
    token_iterator_next(t);
}


static bool token_match_str(const token_t *t, const char *str)
{
    return (TOKEN_LEN(*t) == strlen(str) && !strncmp(t->begin, str, TOKEN_LEN(*t)));
}

// True if current token is the match one wildcard
static bool token_iterator_is_match_1(const token_iterator_t *t)
{
    return TOKEN_LEN(t->token) == 1 &&
        *t->token.begin == t->match_1;
}

// True if current token is the match zero or more wildcard
static bool token_iterator_is_match_glob(const token_iterator_t *t)
{
    return TOKEN_LEN(t->token) == 1 &&
        *t->token.begin == t->match_glob;
}


// Optimize the pattern match code by performing the following
// transformations to the pattern:
// #.* ---> *.#
// #.# ---> #
// This only applies to non-MQTT patterns since in MQTT it is illegal for the #
// to appear anywhere but the last token.
// Returns true if pattern was rewritten.
//
static bool normalize_pattern(qd_parse_tree_type_t type, char *pattern)
{
    token_iterator_t t;
    bool modified = false;
    char *original = NULL;

    if (type == QD_PARSE_TREE_MQTT) return false;

    token_iterator_init(&t, type, pattern);
    while (!token_iterator_done(&t)) {
        if (token_iterator_is_match_glob(&t)) {
            token_t last_token;
            token_iterator_pop(&t, &last_token);
            if (token_iterator_done(&t)) break;
            if (token_iterator_is_match_glob(&t)) {  // #.# --> #
                if (!modified) original = strdup(pattern);
                modified = true;
                char *src = (char *)t.token.begin;
                char *dest = (char *)last_token.begin;
                // note: overlapping strings, can't strcpy
                while (*src)
                    *dest++ = *src++;
                *dest = (char)0;
                t.terminator = dest;
                t.token = last_token;
            } else if (token_iterator_is_match_1(&t)) { // #.* --> *.#
                if (!modified) original = strdup(pattern);
                modified = true;
                *(char *)t.token.begin = t.match_glob;
                *(char *)last_token.begin = t.match_1;
            } else {
                token_iterator_next(&t);
            }
        } else {
            token_iterator_next(&t);
        }
    }

    if (original) {
        qd_log(qd_log_source("DEFAULT"), QD_LOG_NOTICE,
               "configured pattern '%s' optimized and re-written to '%s'",
               original, pattern);
        free(original);
    }

    return modified;
}


// Parse Tree
//
// The pattern is broken up into tokens delimited by a separation character and
// stored in a directed tree graph.  Common pattern prefixes are merged.  This
// allows the lookup alogrithm to quickly isolate those sub-trees that match a
// given input string.
// For example, given the patterns:
//     a.b.c.<...>
//     a.b.d.<...>
//     a.x.y.<...>
// The resulting parse tree would be:
//    a-->b-->c-->...
//    |   +-->d-->...
//    +-->x-->y-->...
//
typedef struct qd_parse_node qd_parse_node_t;
DEQ_DECLARE(qd_parse_node_t, qd_parse_node_list_t);
struct qd_parse_node {
    DEQ_LINKS(qd_parse_node_t); // siblings
    qd_parse_tree_type_t type;  // match algorithm
    bool is_match_1;            // this node is match one wildcard
    bool is_match_glob;         // this node is match zero or more wildcard
    char *token;          // portion of pattern represented by this node
    char *pattern;        // entire normalized pattern matching this node
    // sub-trees of this node:
    qd_parse_node_list_t  children; // all that start with a non-wildcard token
    struct qd_parse_node  *match_1_child;   // starts with a match 1 wildcard
    struct qd_parse_node  *match_glob_child;  // starts with 0 or more wildcard
    // data returned on match against this node:
    void *payload;
    qd_log_source_t *log_source;
};
ALLOC_DECLARE(qd_parse_node_t);
ALLOC_DEFINE(qd_parse_node_t);


static qd_parse_node_t *new_parse_node(const token_t *t, qd_parse_tree_type_t type)
{
    qd_parse_node_t *n = new_qd_parse_node_t();
    if (n) {
        ZERO(n);
        DEQ_ITEM_INIT(n);
        DEQ_INIT(n->children);
        n->type = type;
        n->log_source = qd_log_source("DEFAULT");

        if (t) {  // non-root node
            const size_t tlen = TOKEN_LEN(*t);
            n->token = malloc(tlen + 1);
            strncpy(n->token, t->begin, tlen);
            n->token[tlen] = 0;
            token_iterator_t tmp;
            token_iterator_init(&tmp, type, n->token);
            n->is_match_1 = token_iterator_is_match_1(&tmp);
            n->is_match_glob = token_iterator_is_match_glob(&tmp);
        }
    }
    return n;
}


// return count of child nodes
static int parse_node_child_count(const qd_parse_node_t *n)
{
    return DEQ_SIZE(n->children)
        + (n->match_1_child ? 1 : 0)
        + (n->match_glob_child ? 1 : 0);
}


static void free_parse_node(qd_parse_node_t *n)
{
    assert(parse_node_child_count(n) == 0);
    free(n->token);
    free(n->pattern);
    free_qd_parse_node_t(n);
}


// find immediate child node matching token
static qd_parse_node_t *parse_node_find_child(const qd_parse_node_t *node, const token_t *token)
{
    qd_parse_node_t *child = DEQ_HEAD(node->children);
    while (child && !token_match_str(token, child->token))
        child = DEQ_NEXT(child);
    return child;
}


// Add a new pattern and associated data to the tree.  Returns the old payload
// if the pattern has already been added to the tree.
//
static void *parse_node_add_pattern(qd_parse_node_t *node,
                                    token_iterator_t *key,
                                    const char *pattern,
                                    void *payload)
{
    if (token_iterator_done(key)) {
        // this node's pattern
        void *old;
        if (!node->pattern) {
            node->pattern = strdup(pattern);
        }
        assert(strcmp(node->pattern, pattern) == 0);
        old = node->payload;
        node->payload = payload;
        return old;
    }

    if (token_iterator_is_match_1(key)) {
        if (!node->match_1_child) {
            node->match_1_child = new_parse_node(&key->token,
                                                 node->type);
        }
        token_iterator_next(key);
        return parse_node_add_pattern(node->match_1_child,
                                      key,
                                      pattern,
                                      payload);
    } else if (token_iterator_is_match_glob(key)) {
        if (!node->match_glob_child) {
            node->match_glob_child = new_parse_node(&key->token,
                                                    node->type);
        }
        token_iterator_next(key);
        return parse_node_add_pattern(node->match_glob_child,
                                      key,
                                      pattern,
                                      payload);
    } else {
        // check the children nodes
        token_t current;
        token_iterator_pop(key, &current);

        qd_parse_node_t *child = parse_node_find_child(node, &current);
        if (child) {
            return parse_node_add_pattern(child,
                                          key,
                                          pattern,
                                          payload);
        } else {
            child = new_parse_node(&current, node->type);
            DEQ_INSERT_TAIL(node->children, child);
            return parse_node_add_pattern(child,
                                          key,
                                          pattern,
                                          payload);
        }
    }
}

// remove pattern from the tree.
// return the payload of the removed pattern
static void *parse_node_remove_pattern(qd_parse_node_t *node,
                                       token_iterator_t *key,
                                       const char *pattern)
{
    void *old = NULL;
    if (token_iterator_done(key)) {
        // this node's pattern
        if (node->pattern) {
            assert(strcmp(node->pattern, pattern) == 0);
            free(node->pattern);
            node->pattern = NULL;
        }
        old = node->payload;
        node->payload = NULL;
    } else if (token_iterator_is_match_1(key)) {
        assert(node->match_1_child);
        token_iterator_next(key);
        old = parse_node_remove_pattern(node->match_1_child, key, pattern);
        if (node->match_1_child->pattern == NULL
            && parse_node_child_count(node->match_1_child) == 0) {
            free_parse_node(node->match_1_child);
            node->match_1_child = NULL;
        }
    } else if (token_iterator_is_match_glob(key)) {
        assert(node->match_glob_child);
        token_iterator_next(key);
        old = parse_node_remove_pattern(node->match_glob_child, key, pattern);
        if (node->match_glob_child->pattern == NULL
            && parse_node_child_count(node->match_glob_child) == 0) {
            free_parse_node(node->match_glob_child);
            node->match_glob_child = NULL;
        }
    } else {
        token_t current;
        token_iterator_pop(key, &current);
        qd_parse_node_t *child = parse_node_find_child(node, &current);
        if (child) {
            old = parse_node_remove_pattern(child, key, pattern);
            if (child->pattern == NULL
                && parse_node_child_count(child) == 0) {
                DEQ_REMOVE(node->children, child);
                free_parse_node(child);
            }
        }
    }
    return old;
}


// Find the pattern in the tree, return the payload pointer or NULL if pattern
// is not in the tree
static qd_parse_node_t *parse_node_get_pattern(qd_parse_node_t *node,
                                               token_iterator_t *key,
                                               const char *pattern)
{
    if (!node)
        return NULL;

    if (token_iterator_done(key))
        return node;

    if (token_iterator_is_match_1(key)) {
        token_iterator_next(key);
        return parse_node_get_pattern(node->match_1_child,
                                      key,
                                      pattern);
    } else if (token_iterator_is_match_glob(key)) {
        token_iterator_next(key);
        return parse_node_get_pattern(node->match_glob_child,
                                      key,
                                      pattern);
    } else {
        // check the children nodes
        token_t current;
        token_iterator_pop(key, &current);

        qd_parse_node_t *child = parse_node_find_child(node, &current);
        if (child) {
            return parse_node_get_pattern(child,
                                          key,
                                          pattern);
        }
    }

    return NULL;  // not found
}


static bool parse_node_find(qd_parse_node_t *, token_iterator_t *,
                            qd_parse_tree_visit_t *, void *);


// search the sub-trees of this node.
// This function returns false to stop the search
static bool parse_node_find_children(qd_parse_node_t *node, token_iterator_t *value,
                                     qd_parse_tree_visit_t *callback, void *handle)
{
    if (!token_iterator_done(value)) {

        // check exact match first (precedence)
        if (DEQ_SIZE(node->children) > 0) {
            token_iterator_t tmp = *value;
            token_t child_token;
            token_iterator_pop(&tmp, &child_token);

            qd_parse_node_t *child = parse_node_find_child(node, &child_token);
            if (child) {
                if (!parse_node_find(child, &tmp, callback, handle))
                    return false;
            }
        }

        if (node->match_1_child) {
            token_iterator_t tmp = *value;
            if (!parse_node_find(node->match_1_child, &tmp, callback, handle))
                return false;
        }
    }

    // always try glob - even if empty (it can match empty keys)
    if (node->match_glob_child) {
        token_iterator_t tmp = *value;
        if (!parse_node_find(node->match_glob_child, &tmp, callback, handle))
            return false;
    }

    return true; // continue search
}


// This node matched the last token.
// This function returns false to stop the search
static bool parse_node_find_token(qd_parse_node_t *node, token_iterator_t *value,
                                  qd_parse_tree_visit_t *callback, void *handle)
{
    if (token_iterator_done(value) && node->pattern) {
        // exact match current node
        if (!callback(handle, node->pattern, node->payload))
            return false;
    }

    // no payload or more tokens.  Continue to lower sub-trees. Even if no more
    // tokens (may have a # match)
    return parse_node_find_children(node, value, callback, handle);
}


// this node matches any one token
// returns false to stop the search
static bool parse_node_match_1(qd_parse_node_t *node, token_iterator_t *value,
                               qd_parse_tree_visit_t *callback, void *handle)
{
    // must match exactly one token:
    if (token_iterator_done(value))
        return true;  // no match here, but continue searching

    // pop the topmost token (matched)
    token_iterator_next(value);

    if (token_iterator_done(value) && node->pattern) {
        // exact match current node
        if (!callback(handle, node->pattern, node->payload))
            return false;
    }

    // no payload or more tokens: continue to lower sub-trees
    return parse_node_find_children(node, value, callback, handle);
}


// current node is hash, match the remainder of the string
// return false to stop search
static bool parse_node_match_glob(qd_parse_node_t *node, token_iterator_t *value,
                                  qd_parse_tree_visit_t *callback, void *handle)
{
    // consume each token and look for a match on the
    // remaining key.
    while (!token_iterator_done(value)) {
        if (!parse_node_find_children(node, value, callback, handle))
            return false;
        token_iterator_next(value);
    }

    // this node matches
    if (node->pattern)
        return callback(handle, node->pattern, node->payload);

    return true;
}


// find the payload associated with the input string 'value'
static bool parse_node_find(qd_parse_node_t *node, token_iterator_t *value,
                            qd_parse_tree_visit_t *callback, void *handle)
{
    if (node->is_match_1)
        return parse_node_match_1(node, value, callback, handle);
    else if (node->is_match_glob)
        return parse_node_match_glob(node, value, callback, handle);
    else
        return parse_node_find_token(node, value, callback, handle);
}


static void parse_node_free(qd_parse_node_t *node)
{
    if (node) {
        if (node->match_1_child) parse_node_free(node->match_1_child);
        if (node->match_glob_child) parse_node_free(node->match_glob_child);
        node->match_1_child = node->match_glob_child = NULL;
        while (!DEQ_IS_EMPTY(node->children)) {
            qd_parse_node_t *child = DEQ_HEAD(node->children);
            DEQ_REMOVE_HEAD(node->children);
            parse_node_free(child);
        }

        free_parse_node(node);
    }
}


qd_parse_tree_t *qd_parse_tree_new(qd_parse_tree_type_t type)
{
    return new_parse_node(NULL, type);
}


// find best match for value
//
static bool get_first(void *handle, const char *pattern, void *payload)
{
    *(void **)handle = payload;
    return false;
}

bool qd_parse_tree_retrieve_match(qd_parse_tree_t *node,
                                  const qd_iterator_t *value,
                                  void **payload)
{
    *payload = NULL;
    qd_parse_tree_search(node, value, get_first, payload);
    if (*payload == NULL)
        qd_log(node->log_source, QD_LOG_TRACE, "Parse tree match not found");
    return *payload != NULL;
}


// Invoke callback for each pattern that matches 'value'
void qd_parse_tree_search(qd_parse_tree_t *node,
                          const qd_iterator_t *value,
                          qd_parse_tree_visit_t *callback, void *handle)
{
    token_iterator_t t_iter;
    // @TODO(kgiusti) for now:
    qd_iterator_t *dup = qd_iterator_dup(value);
    char *str = (char *)qd_iterator_copy(dup);
    qd_log(node->log_source, QD_LOG_TRACE, "Parse tree search for '%s'", str);

    token_iterator_init(&t_iter, node->type, str);
    parse_node_find(node, &t_iter, callback, handle);

    free(str);
    qd_iterator_free(dup);
}


// returns old payload or NULL if new
void *qd_parse_tree_add_pattern(qd_parse_tree_t *node,
                                const qd_iterator_t *pattern,
                                void *payload)
{
    token_iterator_t key;
    void *rc = NULL;
    // @TODO(kgiusti) for now:
    qd_iterator_t *dup = qd_iterator_dup(pattern);
    char *str = (char *)qd_iterator_copy(dup);

    normalize_pattern(node->type, str);
    qd_log(node->log_source, QD_LOG_TRACE,
           "Parse tree add address pattern '%s'", str);

    token_iterator_init(&key, node->type, str);
    rc = parse_node_add_pattern(node, &key, str, payload);
    free(str);
    qd_iterator_free(dup);
    return rc;
}


// returns true if pattern exists in tree
bool qd_parse_tree_get_pattern(qd_parse_tree_t *node,
                               const qd_iterator_t *pattern,
                               void **payload)
{
    token_iterator_t key;
    qd_parse_node_t *found = NULL;
    // @TODO(kgiusti) for now:
    qd_iterator_t *dup = qd_iterator_dup(pattern);
    char *str = (char *)qd_iterator_copy(dup);

    normalize_pattern(node->type, (char *)str);
    qd_log(node->log_source, QD_LOG_TRACE,
           "Parse tree get address pattern '%s'", str);

    token_iterator_init(&key, node->type, str);
    found = parse_node_get_pattern(node, &key, str);
    free(str);
    qd_iterator_free(dup);
    *payload = found ? found->payload : NULL;
    return *payload != NULL;
}


// returns the payload void *
void *qd_parse_tree_remove_pattern(qd_parse_tree_t *node,
                                   const qd_iterator_t *pattern)
{
    token_iterator_t key;
    void *rc = NULL;
    // @TODO(kgiusti) for now:
    qd_iterator_t *dup = qd_iterator_dup(pattern);
    char *str = (char *)qd_iterator_copy(dup);

    normalize_pattern(node->type, str);
    qd_log(node->log_source, QD_LOG_TRACE,
           "Parse tree remove address pattern '%s'", str);

    token_iterator_init(&key, node->type, str);
    rc = parse_node_remove_pattern(node, &key, str);
    free(str);
    qd_iterator_free(dup);
    return rc;
}


bool qd_parse_tree_walk(qd_parse_tree_t *node, qd_parse_tree_visit_t *callback, void *handle)
{
    if (node->pattern) {  // terminal node for pattern
        if (!callback(handle, node->pattern, node->payload))
            return false;
    }

    qd_parse_node_t *child = DEQ_HEAD(node->children);
    while (child) {
        if (!qd_parse_tree_walk(child, callback, handle))
            return false;
        child = DEQ_NEXT(child);
    }

    if (node->match_1_child)
        if (!qd_parse_tree_walk(node->match_1_child, callback, handle))
            return false;

    if (node->match_glob_child)
        if (!qd_parse_tree_walk(node->match_glob_child, callback, handle))
            return false;

    return true;
}


bool qd_parse_tree_validate_pattern(const qd_parse_tree_t *tree,
                                    const qd_iterator_t *pattern)
{
    switch (tree->type) {
    case QD_PARSE_TREE_MQTT: {
        // simply ensure that if a '#' is present it is the last token in the
        // pattern
        token_iterator_t ti;
        bool valid = true;
        qd_iterator_t *dup = qd_iterator_dup(pattern);
        char *str = (char *)qd_iterator_copy(dup);
        qd_iterator_free(dup);
        token_iterator_init(&ti, QD_PARSE_TREE_MQTT, str);
        while (!token_iterator_done(&ti)) {
            token_t head;
            token_iterator_pop(&ti, &head);
            if (token_match_str(&head, "#")) {
                valid = token_iterator_done(&ti);
                break;
            }
        }
        free(str);
        return valid;
    }

    case QD_PARSE_TREE_ADDRESS:
    case QD_PARSE_TREE_AMQP_0_10:
    default:
        // never validated these before...
        return true;
    }
}


qd_parse_tree_type_t qd_parse_tree_type(const qd_parse_tree_t *tree)
{
    return tree->type;
}


#if 0
#include <stdio.h>
void qd_parse_tree_dump(qd_parse_node_t *node, int depth)
{
    for (int i = 0; i < depth; i++)
        fprintf(stdout, "  ");
    fprintf(stdout, "token: %s pattern: %s is match 1: %s is glob: %s # childs: %d\n",
            node->token ? node->token : "*NONE*",
            node->pattern ? node->pattern: "*NONE*",
            node->is_match_1 ? "yes" : "no",
            node->is_match_glob ? "yes" : "no",
            parse_node_child_count(node));
    if (node->match_1_child) qd_parse_tree_dump(node->match_1_child, depth + 1);
    if (node->match_glob_child) qd_parse_tree_dump(node->match_glob_child, depth + 1);
    qd_parse_node_t *child = DEQ_HEAD(node->children);
    while (child) {
        qd_parse_tree_dump(child, depth + 1);
        child = DEQ_NEXT(child);
    }
}

void qd_parse_tree_free(qd_parse_node_t *node)
{
    fprintf(stdout, "PARSE TREE DUMP\n");
    qd_parse_tree_dump(node, 4);
    parse_node_free(node);
}

#else

void qd_parse_tree_free(qd_parse_node_t *node)
{
    parse_node_free(node);
}
#endif

//
// parse tree functions using string interface
//

// returns old payload or NULL if new
void *qd_parse_tree_add_pattern_str(qd_parse_tree_t *node,
                                    const char *pattern,
                                    void *payload)
{
    token_iterator_t key;
    void *rc = NULL;
    char *str = strdup(pattern);

    normalize_pattern(node->type, str);
    qd_log(node->log_source, QD_LOG_TRACE,
           "Parse tree(str) add address pattern '%s'", str);

    token_iterator_init(&key, node->type, str);
    rc = parse_node_add_pattern(node, &key, str, payload);
    free(str);
    return rc;
}


// visit each matching pattern that matches value in the order based on the
// precedence rules
void qd_parse_tree_search_str(qd_parse_tree_t *node,
                              const char *value,
                              qd_parse_tree_visit_t *callback, void *handle)
{
    token_iterator_t t_iter;
    // @TODO(kgiusti) for now:
    char *str = strdup(value);
    qd_log(node->log_source, QD_LOG_TRACE, "Parse tree(str) search for '%s'", str);

    token_iterator_init(&t_iter, node->type, str);
    parse_node_find(node, &t_iter, callback, handle);

    free(str);
}


// returns true on match and sets *payload
bool qd_parse_tree_retrieve_match_str(qd_parse_tree_t *tree,
                                      const char *value,
                                      void **payload)
{
    *payload = NULL;
    qd_parse_tree_search_str(tree, value, get_first, payload);
    if (*payload == NULL)
        qd_log(tree->log_source, QD_LOG_TRACE, "Parse tree(str) match not found");
    return *payload != NULL;
}

// returns old payload or NULL if not present
void *qd_parse_tree_remove_pattern_str(qd_parse_tree_t *node,
                                       const char *pattern)
{
    qd_iterator_t *piter = qd_iterator_string(pattern, ITER_VIEW_ALL);
    void *result = qd_parse_tree_remove_pattern(node, piter);
    qd_iterator_free(piter);
    return result;
}
