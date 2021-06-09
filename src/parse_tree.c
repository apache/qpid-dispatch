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

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/hash.h"
#include "qpid/dispatch/log.h"

#include <inttypes.h>
#include <stdio.h>

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
    const char *separators;
    const char *terminator;  // end of entire string
    token_t token;           // token at head of string
    char match_1;            // match any 1 token
    char match_glob;         // match 0 or more tokens
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
const char *qd_parse_address_token_sep()
{
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

struct qd_parse_tree {
    qd_parse_node_t      *root;
    qd_log_source_t      *log_source;
    qd_hash_t            *hash;
    qd_parse_tree_type_t  type;
    uint32_t              next_hkey_prefix;  // next # for hash key prefix
};
ALLOC_DECLARE(qd_parse_tree_t);
ALLOC_DEFINE(qd_parse_tree_t);


typedef enum {
    QD_PARSE_NODE_ROOT,
    QD_PARSE_NODE_TOKEN,
    QD_PARSE_NODE_MATCH_ONE,
    QD_PARSE_NODE_MATCH_GLOB,
} qd_parse_node_type_t;

DEQ_DECLARE(qd_parse_node_t, qd_parse_node_list_t);
struct qd_parse_node {
    DEQ_LINKS(qd_parse_node_t); // siblings
    char *token;                // portion of pattern represented by this node
    char *pattern;              // entire normalized pattern matching this node

    // sub-trees of this node:
    qd_parse_node_t     *match_1_child;     // starts with a match 1 wildcard
    qd_parse_node_t     *match_glob_child;  // starts with 0 or more wildcard
    qd_parse_node_list_t children;          // all that start with a non-wildcard token

    qd_parse_node_t  *parent;         // NULL if root node
    qd_hash_handle_t *handle;
    void             *payload;

    // prefix for hash keys of this node's children
    uint32_t             hkey_prefix;
    qd_parse_node_type_t match_type;
};
ALLOC_DECLARE(qd_parse_node_t);
ALLOC_DEFINE(qd_parse_node_t);


// Hash key for a token node is in the following format:
//  "<parent node hkey-prefix in hex>/<token>
//
#define HKEY_PREFIX_LEN (8 + 1)  // 8 hex chars (32 bit integer) + '/'
static inline void generate_hkey(char *hkey, size_t buf_len, uint32_t pprefix, const token_t *t)
{
    int rc = snprintf(hkey, buf_len, "%"PRIX32"/%.*s", pprefix, (int)TOKEN_LEN(*t), t->begin);
    assert(rc > 0 && rc < buf_len);
    (void)rc;  // prevent warnings for unused var
}


// return count of child nodes
static int parse_node_child_count(const qd_parse_node_t *n)
{
    return DEQ_SIZE(n->children)
        + (n->match_1_child ? 1 : 0)
        + (n->match_glob_child ? 1 : 0);
}


// Free a parse node
static void free_parse_node(qd_parse_tree_t *tree, qd_parse_node_t *n)
{
    if (n) {
        assert(parse_node_child_count(n) == 0);
        free(n->token);
        free(n->pattern);
        if (n->handle) {
            qd_hash_remove_by_handle(tree->hash, n->handle);
            qd_hash_handle_free(n->handle);
        }
        free_qd_parse_node_t(n);
    }
}


// Allocate a new parse tree node.
//
// Allocate a new hash node. If a token is given the hash key for this node is
// generated by concatenating the parent prefix with the token.
//
static qd_parse_node_t *new_parse_node(qd_parse_tree_t *tree,
                                       qd_parse_node_type_t match_type,
                                       const token_t *t,
                                       qd_parse_node_t *parent)
{
    qd_parse_node_t *n = new_qd_parse_node_t();
    if (n) {
        ZERO(n);
        DEQ_ITEM_INIT(n);
        DEQ_INIT(n->children);
        n->match_type = match_type;
        // generate a new hash key prefix for this node's children
        n->hkey_prefix = tree->next_hkey_prefix++;

        if (match_type == QD_PARSE_NODE_TOKEN) {
            assert(t);
            assert(parent);
            const size_t tlen = TOKEN_LEN(*t);
            n->token = malloc(tlen + 1);
            if (!n->token) {
                free_qd_parse_node_t(n);
                return 0;
            }
            strncpy(n->token, t->begin, tlen);
            n->token[tlen] = 0;
            {
                const size_t hkey_size = HKEY_PREFIX_LEN + tlen + 1;
                char *hkey = qd_malloc(hkey_size);
                generate_hkey(hkey, hkey_size, parent->hkey_prefix, t);

                if (qd_hash_insert_str(tree->hash, (unsigned char *)hkey, (void *)n, &n->handle) != QD_ERROR_NONE) {
                    free_parse_node(tree, n);
                    free(hkey);
                    return 0;
                }
                free(hkey);
                n->parent = parent;
            }
        }
    }

    return n;
}


// find immediate child node matching token
static qd_parse_node_t *parse_node_find_child(qd_parse_tree_t *tree, const qd_parse_node_t *node, const token_t *token)
{
    qd_parse_node_t *child = 0;
    const size_t tlen = TOKEN_LEN(*token);
    const size_t hkey_size = HKEY_PREFIX_LEN + tlen + 1;
    char *hkey = qd_malloc(hkey_size);
    generate_hkey(hkey, hkey_size, node->hkey_prefix, token);
    qd_hash_retrieve_str(tree->hash, (const unsigned char *)hkey, (void **)&child);
    if (child) {
        assert(child->parent == node);
    }
    free(hkey);
    return child;
}


// Add a new pattern and associated data to the tree.
// Return QD_ERROR_ALREADY_EXISTS if the pattern is already in the tree.
// caller transfers ownership of "pattern"
//
static qd_error_t parse_node_add_pattern(qd_parse_tree_t *tree, char *pattern, void *payload)
{
    qd_parse_node_t *node = tree->root;
    qd_error_t result = QD_ERROR_NONE;
    token_iterator_t iterator;

    normalize_pattern(tree->type, pattern);

    // worst case maximum for hash key if pattern is a single token
    const size_t buf_size = HKEY_PREFIX_LEN + strlen(pattern) + 1;
    char *hkey = malloc(buf_size);
    if (!hkey) {
        result = qd_error(QD_ERROR_ALLOC, "Pattern '%s' not added to parse tree", pattern);
        free(pattern);
        return result;
    }

    token_iterator_init(&iterator, tree->type, pattern);

    //
    // walk the iterator through the tree adding nodes for each token/wildcard as needed
    //
    while (!token_iterator_done(&iterator)) {

        if (token_iterator_is_match_1(&iterator)) {
            if (!node->match_1_child) {
                node->match_1_child = new_parse_node(tree, QD_PARSE_NODE_MATCH_ONE, 0, node);
                if (!node->match_1_child) {
                    result = qd_error(QD_ERROR_ALLOC, "Pattern '%s' not added to parse tree", pattern);
                    break;
                }
                node->match_1_child->parent = node;
            }
            node = node->match_1_child;
            token_iterator_next(&iterator);

        } else if (token_iterator_is_match_glob(&iterator)) {
            if (!node->match_glob_child) {
                node->match_glob_child = new_parse_node(tree, QD_PARSE_NODE_MATCH_GLOB, 0, node);
                if (!node->match_glob_child) {
                    result = qd_error(QD_ERROR_ALLOC, "Pattern '%s' not added to parse tree", pattern);
                    break;
                }
                node->match_glob_child->parent = node;
            }
            node = node->match_glob_child;
            token_iterator_next(&iterator);

        } else { // non-wildcard token
            token_t new_token;
            token_iterator_pop(&iterator, &new_token);
            generate_hkey(hkey, buf_size, node->hkey_prefix, &new_token);

            qd_parse_node_t *child = 0;
            qd_hash_retrieve_str(tree->hash, (const unsigned char *)hkey, (void **)&child);

            if (child) {
                assert(child->parent == node);
                node = child;
            } else {
                // add new node for new_token
                child = new_parse_node(tree, QD_PARSE_NODE_TOKEN, &new_token, node);
                if (!child) {
                    result = qd_error(QD_ERROR_ALLOC, "Pattern '%s' not added to parse tree", pattern);
                    break;
                }
                DEQ_INSERT_TAIL(node->children, child);
                node = child;
            }
        }
    }

    // done iterating over pattern.

    if (result == QD_ERROR_NONE) {
        if (node == tree->root) {
            result = qd_error(QD_ERROR_VALUE, "Invalid pattern '%s' not added to parse tree", pattern);
        } else if (node->pattern) {
            result = qd_error(QD_ERROR_ALREADY_EXISTS, "Duplicate pattern '%s' not added to parse tree", pattern);
        } else {
            node->pattern = pattern;
            pattern = 0;
            node->payload = payload;
            qd_log(tree->log_source, QD_LOG_TRACE, "Parse tree add pattern '%s'", node->pattern);
        }
    }

    free(pattern);
    free(hkey);

    return result;
}


// Find the leaf node matching the pattern.  Returns 0 if pattern is not in the tree
//
static qd_parse_node_t *parse_node_get_pattern(qd_parse_tree_t *tree, char *pattern)
{
    qd_parse_node_t *node = tree->root;
    token_iterator_t iterator;

    normalize_pattern(tree->type, pattern);

    // worst case maximum for hash key if pattern is a single token
    const size_t buf_size = HKEY_PREFIX_LEN + strlen(pattern) + 1;
    char *hkey = malloc(buf_size);
    if (!hkey) {
        return 0;
    }

    token_iterator_init(&iterator, tree->type, pattern);

    while (node && !token_iterator_done(&iterator)) {

        if (token_iterator_is_match_1(&iterator)) {
            node = node->match_1_child;
            token_iterator_next(&iterator);

        } else if (token_iterator_is_match_glob(&iterator)) {
            node = node->match_glob_child;
            token_iterator_next(&iterator);

        } else {
            token_t new_token;
            token_iterator_pop(&iterator, &new_token);

            // search for new_token child
            assert(node->hkey_prefix);
            generate_hkey(hkey, buf_size, node->hkey_prefix, &new_token);

            qd_parse_node_t *child = 0;
            qd_hash_retrieve_str(tree->hash, (const unsigned char *)hkey, (void **)&child);

            assert(!child || child->parent == node);
            node = child;
        }
    }

    free(hkey);

    if (node && node->pattern) {
        assert(strcmp(node->pattern, pattern) == 0);
        return node;
    }

    return 0;
}


static bool parse_node_find(qd_parse_tree_t *, qd_parse_node_t *, token_iterator_t *,
                            qd_parse_tree_visit_t *, void *);


// search the sub-trees of this node.
// This function returns false to stop the search
static bool parse_node_find_children(qd_parse_tree_t *tree, qd_parse_node_t *node, token_iterator_t *value,
                                     qd_parse_tree_visit_t *callback, void *handle)
{
    if (!token_iterator_done(value)) {

        // check exact match first (precedence)
        if (DEQ_SIZE(node->children) > 0) {
            token_iterator_t tmp = *value;
            token_t child_token;
            token_iterator_pop(&tmp, &child_token);

            qd_parse_node_t *child = parse_node_find_child(tree, node, &child_token);
            if (child) {
                if (!parse_node_find(tree, child, &tmp, callback, handle))
                    return false;
            }
        }

        if (node->match_1_child) {
            token_iterator_t tmp = *value;
            if (!parse_node_find(tree, node->match_1_child, &tmp, callback, handle))
                return false;
        }
    }

    // always try glob - even if empty (it can match empty keys)
    if (node->match_glob_child) {
        token_iterator_t tmp = *value;
        if (!parse_node_find(tree, node->match_glob_child, &tmp, callback, handle))
            return false;
    }

    return true; // continue search
}


// This node matched the last token.
// This function returns false to stop the search
static bool parse_node_find_token(qd_parse_tree_t *tree, qd_parse_node_t *node, token_iterator_t *value,
                                  qd_parse_tree_visit_t *callback, void *handle)
{
    if (token_iterator_done(value) && node->pattern) {
        // exact match current node
        if (!callback(handle, node->pattern, node->payload))
            return false;
    }

    // no payload or more tokens.  Continue to lower sub-trees. Even if no more
    // tokens (may have a # match)
    return parse_node_find_children(tree, node, value, callback, handle);
}


// this node matches any one token
// returns false to stop the search
static bool parse_node_match_1(qd_parse_tree_t *tree, qd_parse_node_t *node, token_iterator_t *value,
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
    return parse_node_find_children(tree, node, value, callback, handle);
}


// current node is hash, match the remainder of the string
// return false to stop search
static bool parse_node_match_glob(qd_parse_tree_t *tree, qd_parse_node_t *node, token_iterator_t *value,
                                  qd_parse_tree_visit_t *callback, void *handle)
{
    // consume each token and look for a match on the
    // remaining key.
    while (!token_iterator_done(value)) {
        if (!parse_node_find_children(tree, node, value, callback, handle))
            return false;
        token_iterator_next(value);
    }

    // this node matches
    if (node->pattern)
        return callback(handle, node->pattern, node->payload);

    return true;
}


// find the payload associated with the input string 'value'
static bool parse_node_find(qd_parse_tree_t *tree, qd_parse_node_t *node, token_iterator_t *value,
                            qd_parse_tree_visit_t *callback, void *handle)
{
    switch (node->match_type) {
    case QD_PARSE_NODE_MATCH_ONE:
        return parse_node_match_1(tree, node, value, callback, handle);
    case QD_PARSE_NODE_MATCH_GLOB:
        return parse_node_match_glob(tree, node, value, callback, handle);
    case QD_PARSE_NODE_TOKEN:
        return parse_node_find_token(tree, node, value, callback, handle);
    case QD_PARSE_NODE_ROOT:
        return parse_node_find_children(tree, node, value, callback, handle);
    }
    return true;
}


static void parse_node_free(qd_parse_tree_t *tree, qd_parse_node_t *node)
{
    if (node) {
        if (node->match_1_child) parse_node_free(tree, node->match_1_child);
        if (node->match_glob_child) parse_node_free(tree, node->match_glob_child);
        node->match_1_child = node->match_glob_child = NULL;
        while (!DEQ_IS_EMPTY(node->children)) {
            qd_parse_node_t *child = DEQ_HEAD(node->children);
            DEQ_REMOVE_HEAD(node->children);
            parse_node_free(tree, child);
        }

        free_parse_node(tree, node);
    }
}


qd_parse_tree_t *qd_parse_tree_new(qd_parse_tree_type_t type)
{

    qd_parse_tree_t *tree = new_qd_parse_tree_t();
    if (tree) {
        ZERO(tree);
        tree->type = type;
        tree->log_source = qd_log_source("DEFAULT");
        tree->next_hkey_prefix = 1;
        tree->root = new_parse_node(tree, QD_PARSE_NODE_ROOT, 0, 0);
        if (!tree->root) {
            free_qd_parse_tree_t(tree);
            return 0;
        }
        tree->hash = qd_hash(10, 32, false);
        if (!tree->hash) {
            parse_node_free(tree, tree->root);
            free_qd_parse_tree_t(tree);
            return 0;
        }
    }

    return tree;
}


// find best match for value
//
static bool get_first(void *handle, const char *pattern, void *payload)
{
    *(void **)handle = payload;
    return false;
}

bool qd_parse_tree_retrieve_match(qd_parse_tree_t *tree,
                                  const qd_iterator_t *value,
                                  void **payload)
{
    *payload = NULL;
    qd_parse_tree_search(tree, value, get_first, payload);
    if (*payload == NULL)
        qd_log(tree->log_source, QD_LOG_TRACE, "Parse tree match not found");
    return *payload != NULL;
}


// Invoke callback for each pattern that matches 'value'
void qd_parse_tree_search(qd_parse_tree_t *tree,
                          const qd_iterator_t *value,
                          qd_parse_tree_visit_t *callback, void *handle)
{
    token_iterator_t t_iter;
    char *str = (char *)qd_iterator_copy_const(value);
    qd_log(tree->log_source, QD_LOG_TRACE, "Parse tree search for '%s'", str);

    token_iterator_init(&t_iter, tree->type, str);
    parse_node_find(tree, tree->root, &t_iter, callback, handle);

    free(str);
}


qd_error_t qd_parse_tree_add_pattern(qd_parse_tree_t *tree,
                                     const qd_iterator_t *pattern,
                                     void *payload)
{
    char *str = (char *)qd_iterator_copy_const(pattern);
    if (!str)
        return QD_ERROR_ALLOC;

    return parse_node_add_pattern(tree, str, payload);
}


// returns true if pattern exists in tree
bool qd_parse_tree_get_pattern(qd_parse_tree_t *tree,
                               const qd_iterator_t *pattern,
                               void **payload)
{
    char *str = (char *)qd_iterator_copy_const(pattern);
    if (!str)
        return false;

    qd_parse_node_t *found = parse_node_get_pattern(tree, str);
    free(str);
    *payload = found ? found->payload : NULL;
    return !!found;
}


// returns the payload void *
void *qd_parse_tree_remove_pattern(qd_parse_tree_t *tree,
                                   const qd_iterator_t *pattern)
{
    char *str = (char *)qd_iterator_copy_const(pattern);
    if (!str)
        return 0;

    qd_parse_node_t *node = parse_node_get_pattern(tree, str);
    if (!node) {
        free(str);
        return 0;
    }

    assert(strcmp(node->pattern, str) == 0);
    free(str);

    void *value = node->payload;

    // now clean up this node.  Be aware we can only free the entire node if it does not have children.
    // If it has children just zero out the pattern field indicates it is no longer a terminal node.
    // The fun happens if it has no children: in this case free the node then
    // see if the parent node is now childless and NOT a terminal node. If so
    // clean it up as well.  Repeat.

    free(node->pattern);
    node->pattern = 0;
    node->payload = 0;
    qd_parse_node_t *parent = node->parent;

    while (node && node->pattern == 0 && parse_node_child_count(node) == 0 && parent) {

        switch (node->match_type) {
        case QD_PARSE_NODE_MATCH_ONE:
            assert(parent->match_1_child == node);
            parent->match_1_child = 0;
            break;
        case QD_PARSE_NODE_MATCH_GLOB:
            assert(parent->match_glob_child == node);
            parent->match_glob_child = 0;
            break;
        case QD_PARSE_NODE_TOKEN:
            DEQ_REMOVE(parent->children, node);
            break;
        case QD_PARSE_NODE_ROOT:
        default:
            // cannot get here
            assert(false);
            break;
        }
        free_parse_node(tree, node);
        node = parent;
        parent = node->parent;
    }

    return value;
}


static bool parse_tree_walk(qd_parse_node_t *node, qd_parse_tree_visit_t *callback, void *handle)
{
    if (node->pattern) {  // terminal node for pattern
        if (!callback(handle, node->pattern, node->payload))
            return false;
    }

    qd_parse_node_t *child = DEQ_HEAD(node->children);
    while (child) {
        if (!parse_tree_walk(child, callback, handle))
            return false;
        child = DEQ_NEXT(child);
    }

    if (node->match_1_child)
        if (!parse_tree_walk(node->match_1_child, callback, handle))
            return false;

    if (node->match_glob_child)
        if (!parse_tree_walk(node->match_glob_child, callback, handle))
            return false;

    return true;
}


bool qd_parse_tree_walk(qd_parse_tree_t *tree, qd_parse_tree_visit_t *callback, void *handle)
{
    return parse_tree_walk(tree->root, callback, handle);
}


bool qd_parse_tree_validate_pattern(const qd_parse_tree_t *tree,
                                    const qd_iterator_t *pattern)
{
    char *str = (char *)qd_iterator_copy_const(pattern);
    if (!str)
        return false;

    token_iterator_t ti;
    token_iterator_init(&ti, tree->type, str);
    if (token_iterator_done(&ti)) {
        // empty iterator
        free(str);
        return false;
    }

    if (tree->type == QD_PARSE_TREE_MQTT) {
        // simply ensure that if a '#' is present it is the last token in the
        // pattern
        bool valid = true;
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

    free(str);
    return true;
}


qd_parse_tree_type_t qd_parse_tree_type(const qd_parse_tree_t *tree)
{
    return tree->type;
}


static void _parse_tree_free(qd_parse_tree_t *tree)
{
    if (tree) {
        parse_node_free(tree, tree->root);
        qd_hash_free(tree->hash);
        free_qd_parse_tree_t(tree);
    }
}

#if 0

static void _node_dump(const qd_parse_node_t *node, const int depth)
{
    for (int i = 0; i < depth; i++)
        fprintf(stdout, "  ");
    char *type = (node->match_type == QD_PARSE_NODE_ROOT
                  ? "ROOT"
                  : (node->match_type == QD_PARSE_NODE_TOKEN
                     ? "TOKEN"
                     : (node->match_type == QD_PARSE_NODE_MATCH_ONE
                        ? "STAR"
                        : "GLOB")));
    fprintf(stdout, "token: %s pattern: %s type: %s # childs: %d hkey: %s hprefix %"PRIX32" payload: %p\n",
            node->token ? node->token : "*NONE*",
            node->pattern ? node->pattern: "*NONE*",
            type,
            parse_node_child_count(node),
            (node->handle ? (const char *)qd_hash_key_by_handle(node->handle) : "*NONE*"),
            node->hkey_prefix,
            node->payload);

    if (node->match_1_child) _node_dump(node->match_1_child, depth + 1);
    if (node->match_glob_child) _node_dump(node->match_glob_child, depth + 1);
    qd_parse_node_t *child = DEQ_HEAD(node->children);
    while (child) {
        _node_dump(child, depth + 1);
        child = DEQ_NEXT(child);
    }
}

void qd_parse_tree_dump(const qd_parse_tree_t *tree)
{
    if (tree && tree->root) {
        _node_dump(tree->root, 0);
        fflush(stdout);
    }
}

void qd_parse_tree_free(qd_parse_tree_t *tree)
{
    fprintf(stdout, "PARSE TREE DUMP\n");
    fprintf(stdout, "===============\n");
    qd_parse_tree_dump(tree);
    _parse_tree_free(tree);
}

#else

void qd_parse_tree_free(qd_parse_tree_t *tree)
{
    _parse_tree_free(tree);
}
#endif

//
// parse tree functions using string interface
//

qd_error_t qd_parse_tree_add_pattern_str(qd_parse_tree_t *tree,
                                         const char *pattern,
                                         void *payload)
{
    char *str = strdup(pattern);
    if (!str)
        return QD_ERROR_ALLOC;

    return parse_node_add_pattern(tree, str, payload);
}


// visit each matching pattern that matches value in the order based on the
// precedence rules
void qd_parse_tree_search_str(qd_parse_tree_t *tree,
                              const char *value,
                              qd_parse_tree_visit_t *callback, void *handle)
{
    token_iterator_t t_iter;
    // @TODO(kgiusti) for now:
    char *str = strdup(value);
    qd_log(tree->log_source, QD_LOG_TRACE, "Parse tree(str) search for '%s'", str);

    token_iterator_init(&t_iter, tree->type, str);
    parse_node_find(tree, tree->root, &t_iter, callback, handle);

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
void *qd_parse_tree_remove_pattern_str(qd_parse_tree_t *tree,
                                       const char *pattern)
{
    qd_iterator_t *piter = qd_iterator_string(pattern, ITER_VIEW_ALL);
    void *result = qd_parse_tree_remove_pattern(tree, piter);
    qd_iterator_free(piter);
    return result;
}
