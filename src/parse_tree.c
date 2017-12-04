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

// Wildcard pattern matching:
// '*' - match exactly one single token
// '#' - match zero or more tokens
// * has higher precedence than #: "a/b" will match pattern "a/*" before "a/#"
static const char STAR = '*';
static const char HASH = '#';


// token parsing
// parse a string of tokens separated by a boundary character (. or /).
//
// The format of a pattern string is a series of tokens. A
// token can contain any characters but '#', '*', or the boundary character.
//
// TODO(kgiusti): really should create a token parsing qd_iterator_t view.
// Then all this token/token_iterator stuff can be removed...
//
const char * const QD_PARSE_TREE_TOKEN_SEP = "./";
typedef struct token {
    const char *begin;
    const char *end;
} token_t;
#define TOKEN_LEN(t) ((t).end - (t).begin)

// for iterating over a string of tokens:
typedef struct token_iterator {
    token_t token;           // token at head of string
    const char *terminator;  // end of entire string
} token_iterator_t;


static void token_iterator_init(token_iterator_t *t, const char *str)
{
    while (*str && strchr(QD_PARSE_TREE_TOKEN_SEP, *str))
        str++;  // skip any leading separators
    const char *tend = strpbrk(str, QD_PARSE_TREE_TOKEN_SEP);
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
        tend = strpbrk(t->token.begin, QD_PARSE_TREE_TOKEN_SEP);
        t->token.end = (tend) ? tend : t->terminator;
    }
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

// True if token matches the given char value
static bool token_iterator_match_char(const token_iterator_t *t,
                                      const char c)
{
    return TOKEN_LEN(t->token) == 1 && *t->token.begin == c;
}


// Optimize the pattern match code by performing the following
// transformations to the pattern:
// #.* ---> *.#
// #.# ---> #
//
static bool normalize_pattern(char *pattern)
{
    token_iterator_t t;
    bool modified = false;
    char *original = NULL;

    token_iterator_init(&t, pattern);
    while (!token_iterator_done(&t)) {
        if (token_iterator_match_char(&t, HASH)) {
            token_t last_token;
            token_iterator_pop(&t, &last_token);
            if (token_iterator_done(&t)) break;
            if (token_iterator_match_char(&t, HASH)) {  // #.# --> #
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
            } else if (token_iterator_match_char(&t, STAR)) { // #.* --> *.#
                if (!modified) original = strdup(pattern);
                modified = true;
                *(char *)t.token.begin = HASH;
                *(char *)last_token.begin = STAR;
            } else {
                token_iterator_next(&t);
            }
        } else {
            token_iterator_next(&t);
        }
    }

    if (original) {
        qd_log(qd_log_source("DEFAULT"), QD_LOG_NOTICE,
               "configured address '%s' optimized and re-written to '%s'",
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
    char *token;                // portion of pattern represented by this node
    bool is_star;
    bool is_hash;
    char *pattern;        // entire normalized pattern matching this node
    qd_parse_node_list_t  children;
    struct qd_parse_node  *star_child;
    struct qd_parse_node  *hash_child;
    void *payload;      // data returned on match against this node
    qd_log_source_t *log_source;
};
ALLOC_DECLARE(qd_parse_node_t);
ALLOC_DEFINE(qd_parse_node_t);


static qd_parse_node_t *new_parse_node(const token_t *t)
{
    qd_parse_node_t *n = new_qd_parse_node_t();
    if (n) {
        DEQ_ITEM_INIT(n);
        DEQ_INIT(n->children);
        n->payload = NULL;
        n->pattern = NULL;
        n->star_child = n->hash_child = NULL;
        n->log_source = qd_log_source("DEFAULT");

        if (t) {
            const size_t tlen = TOKEN_LEN(*t);
            n->token = malloc(tlen + 1);
            strncpy(n->token, t->begin, tlen);
            n->token[tlen] = 0;
            n->is_star = (tlen == 1 && *t->begin == STAR);
            n->is_hash = (tlen == 1 && *t->begin == HASH);
        } else {  // root
            n->token = NULL;
            n->is_star = n->is_hash = false;
        }
    }
    return n;
}


// return count of child nodes
static int parse_node_child_count(const qd_parse_node_t *n)
{
    return DEQ_SIZE(n->children)
        + (n->star_child ? 1 : 0)
        + (n->hash_child ? 1 : 0);
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

    if (token_iterator_match_char(key, STAR)) {
        if (!node->star_child) {
            node->star_child = new_parse_node(&key->token);
        }
        token_iterator_next(key);
        return parse_node_add_pattern(node->star_child,
                                      key,
                                      pattern,
                                      payload);
    } else if (token_iterator_match_char(key, HASH)) {
        if (!node->hash_child) {
            node->hash_child = new_parse_node(&key->token);
        }
        token_iterator_next(key);
        return parse_node_add_pattern(node->hash_child,
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
            child = new_parse_node(&current);
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
    } else if (token_iterator_match_char(key, STAR)) {
        assert(node->star_child);
        token_iterator_next(key);
        old = parse_node_remove_pattern(node->star_child, key, pattern);
        if (node->star_child->pattern == NULL
            && parse_node_child_count(node->star_child) == 0) {
            free_parse_node(node->star_child);
            node->star_child = NULL;
        }
    } else if (token_iterator_match_char(key, HASH)) {
        assert(node->hash_child);
        token_iterator_next(key);
        old = parse_node_remove_pattern(node->hash_child, key, pattern);
        if (node->hash_child->pattern == NULL
            && parse_node_child_count(node->hash_child) == 0) {
            free_parse_node(node->hash_child);
            node->hash_child = NULL;
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

    if (token_iterator_match_char(key, STAR)) {
        token_iterator_next(key);
        return parse_node_get_pattern(node->star_child,
                                      key,
                                      pattern);
    } else if (token_iterator_match_char(key, HASH)) {
        token_iterator_next(key);
        return parse_node_get_pattern(node->hash_child,
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

        if (node->star_child) {
            token_iterator_t tmp = *value;
            if (!parse_node_find(node->star_child, &tmp, callback, handle))
                return false;
        }
    }

    // always try glob - even if empty (it can match empty keys)
    if (node->hash_child) {
        token_iterator_t tmp = *value;
        if (!parse_node_find(node->hash_child, &tmp, callback, handle))
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
static bool parse_node_find_star(qd_parse_node_t *node, token_iterator_t *value,
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
static bool parse_node_find_hash(qd_parse_node_t *node, token_iterator_t *value,
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
    if (node->is_star)
        return parse_node_find_star(node, value, callback, handle);
    else if (node->is_hash)
        return parse_node_find_hash(node, value, callback, handle);
    else
        return parse_node_find_token(node, value, callback, handle);
}


static void parse_node_free(qd_parse_node_t *node)
{
    if (node) {
        if (node->star_child) parse_node_free(node->star_child);
        if (node->hash_child) parse_node_free(node->hash_child);
        node->star_child = node->hash_child = NULL;
        while (!DEQ_IS_EMPTY(node->children)) {
            qd_parse_node_t *child = DEQ_HEAD(node->children);
            DEQ_REMOVE_HEAD(node->children);
            parse_node_free(child);
        }

        free_parse_node(node);
    }
}


qd_parse_tree_t *qd_parse_tree_new()
{
    return new_parse_node(NULL);
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

    token_iterator_init(&t_iter, str);
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

    normalize_pattern(str);
    qd_log(node->log_source, QD_LOG_TRACE,
           "Parse tree add address pattern '%s'", str);

    token_iterator_init(&key, str);
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

    normalize_pattern((char *)str);
    qd_log(node->log_source, QD_LOG_TRACE,
           "Parse tree get address pattern '%s'", str);

    token_iterator_init(&key, str);
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

    normalize_pattern(str);
    qd_log(node->log_source, QD_LOG_TRACE,
           "Parse tree remove address pattern '%s'", str);

    token_iterator_init(&key, str);
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

    if (node->star_child)
        if (!qd_parse_tree_walk(node->star_child, callback, handle))
            return false;

    if (node->hash_child)
        if (!qd_parse_tree_walk(node->hash_child, callback, handle))
            return false;

    return true;
}


#if 0
#include <stdio.h>
void qd_parse_tree_dump(qd_parse_node_t *node, int depth)
{
    for (int i = 0; i < depth; i++)
        fprintf(stdout, "  ");
    fprintf(stdout, "token: %s pattern: %s is star: %s is hash: %s # childs: %d\n",
            node->token ? node->token : "*NONE*",
            node->pattern ? node->pattern: "*NONE*",
            node->is_star ? "yes" : "no",
            node->is_hash ? "yes" : "no",
            parse_node_child_count(node));
    if (node->star_child) qd_parse_tree_dump(node->star_child, depth + 1);
    if (node->hash_child) qd_parse_tree_dump(node->hash_child, depth + 1);
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

