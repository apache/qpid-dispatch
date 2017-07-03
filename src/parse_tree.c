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


// Wildcard pattern matching:
// '*' - match exactly one single token
// '#' - match zero or more tokens
// * has higher precedence than #: "a/b" will match pattern "a/*" before "a/#"
static const char STAR = '*';
static const char HASH = '#';


// token parsing
// parse a string of tokens separated by a boundary character.
//
// The format of a pattern string is a series of tokens. A
// token can contain any characters but '#', '*', or the boundary character.
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
    token_t token;           // token at head of string
    const char *terminator;  // end of entire string
    char separator;          // delimits tokens
} token_iterator_t;


static void token_iterator_init(token_iterator_t *t, const char *str,
                                char separator)
{
    const char *tend = strchr(str, separator);
    t->terminator = str + strlen(str);
    t->token.begin = str;
    t->token.end = (tend) ? tend : t->terminator;
    t->separator = separator;
}


static void token_iterator_next(token_iterator_t *t)
{
    if (t->token.end == t->terminator) {
        t->token.begin = t->terminator;
    } else {
        const char *tend;
        t->token.begin = t->token.end + 1;
        tend = strchr(t->token.begin, t->separator);
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
    return !strncmp(t->begin, str, TOKEN_LEN(*t));
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
static char *normalize_pattern(char *pattern, char token_sep)
{
    token_iterator_t t;

    token_iterator_init(&t, pattern, token_sep);
    while (!token_iterator_done(&t)) {
        if (token_iterator_match_char(&t, HASH)) {
            token_t last_token;
            token_iterator_pop(&t, &last_token);
            if (token_iterator_done(&t)) break;
            if (token_iterator_match_char(&t, HASH)) {  // #.# --> #
                char *src = (char *)t.token.begin;
                char *dest = (char *)last_token.begin;
                // note: overlapping strings, can't strcpy
                while (*src)
                    *dest++ = *src++;
                *dest = (char)0;
                t.terminator = dest;
                t.token = last_token;
            } else if (token_iterator_match_char(&t, STAR)) { // #.* --> *.#
                *(char *)t.token.begin = HASH;
                *(char *)last_token.begin = STAR;
            } else {
                token_iterator_next(&t);
            }
        } else {
            token_iterator_next(&t);
        }
    }
    return pattern;
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

DEQ_DECLARE(parse_node_t, parse_node_list_t);
struct parse_node {
    DEQ_LINKS(parse_node_t);    // siblings
    char *token;                // portion of pattern represented by this node
    char token_sep;             // boundary character between tokens
    bool is_star;
    bool is_hash;
    char *pattern;        // entire normalized pattern matching this node
    parse_node_list_t  children;
    struct parse_node  *star_child;
    struct parse_node  *hash_child;
    void *payload;      // data returned on match against this node
};
ALLOC_DECLARE(parse_node_t);
ALLOC_DEFINE(parse_node_t);


static parse_node_t *new_parse_node(const token_t *t, char token_sep)
{
    parse_node_t *n = new_parse_node_t();
    if (n) {
        DEQ_ITEM_INIT(n);
        DEQ_INIT(n->children);
        n->payload = NULL;
        n->pattern = NULL;
        n->star_child = n->hash_child = NULL;
        n->token_sep = token_sep;

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
static int parse_node_child_count(const parse_node_t *n)
{
    return DEQ_SIZE(n->children)
        + n->star_child ? 1 : 0
        + n->hash_child ? 1 : 0;
}


static void free_parse_node(parse_node_t *n)
{
    assert(parse_node_child_count(n) == 0);
    free(n->token);
    free(n->pattern);
    free_parse_node_t(n);
}


// find immediate child node matching token
static parse_node_t *parse_node_find_child(const parse_node_t *node, const token_t *token)
{
    parse_node_t *child = DEQ_HEAD(node->children);
    while (child && !token_match_str(token, child->token))
        child = DEQ_NEXT(child);
    return child;
}


// Add a new pattern and associated data to the tree.  Returns the old payload
// if the pattern has already been added to the tree.
//
static void *parse_node_add_pattern(parse_node_t *node,
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
            node->star_child = new_parse_node(&key->token,
                                              node->token_sep);
        }
        token_iterator_next(key);
        return parse_node_add_pattern(node->star_child,
                                      key,
                                      pattern,
                                      payload);
    } else if (token_iterator_match_char(key, HASH)) {
        if (!node->hash_child) {
            node->hash_child = new_parse_node(&key->token,
                                              node->token_sep);
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

        parse_node_t *child = parse_node_find_child(node, &current);
        if (child) {
            return parse_node_add_pattern(child,
                                          key,
                                          pattern,
                                          payload);
        } else {
            child = new_parse_node(&current, node->token_sep);
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
static void *parse_node_remove_pattern(parse_node_t *node,
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
        parse_node_t *child = parse_node_find_child(node, &current);
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


static bool parse_node_find(parse_node_t *, token_iterator_t *,
                            parse_tree_visit_t *, void *);


// search the sub-trees of this node.
// This function returns false to stop the search
static bool parse_node_find_children(parse_node_t *node, token_iterator_t *value,
                                     parse_tree_visit_t *callback, void *handle)
{
    if (!token_iterator_done(value)) {

        // check exact match first (precedence)
        if (DEQ_SIZE(node->children) > 0) {
            token_iterator_t tmp = *value;
            token_t child_token;
            token_iterator_pop(&tmp, &child_token);

            parse_node_t *child = parse_node_find_child(node, &child_token);
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
static bool parse_node_find_token(parse_node_t *node, token_iterator_t *value,
                                  parse_tree_visit_t *callback, void *handle)
{
    if (token_iterator_done(value) && node->pattern) {
        // exact match current node
        return callback(handle, node->pattern, node->payload);
    }

    // no payload or more tokens.  Continue to lower sub-trees. Even if no more
    // tokens (may have a # match)
    return parse_node_find_children(node, value, callback, handle);
}


// this node matches any one token
// returns false to stop the search
static bool parse_node_find_star(parse_node_t *node, token_iterator_t *value,
                                 parse_tree_visit_t *callback, void *handle)
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
static bool parse_node_find_hash(parse_node_t *node, token_iterator_t *value,
                                 parse_tree_visit_t *callback, void *handle)
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
static bool parse_node_find(parse_node_t *node, token_iterator_t *value,
                            parse_tree_visit_t *callback, void *handle)
{
    if (node->is_star)
        return parse_node_find_star(node, value, callback, handle);
    else if (node->is_hash)
        return parse_node_find_hash(node, value, callback, handle);
    else
        return parse_node_find_token(node, value, callback, handle);
}


parse_node_t *parse_tree_new(char token_sep)
{
    return new_parse_node(NULL, token_sep);
}


void parse_tree_free(parse_node_t *node)
{
    if (node) {
        if (node->star_child) parse_tree_free(node->star_child);
        if (node->hash_child) parse_tree_free(node->hash_child);
        node->star_child = node->hash_child = NULL;
        while (!DEQ_IS_EMPTY(node->children)) {
            parse_node_t *child = DEQ_HEAD(node->children);
            DEQ_REMOVE_HEAD(node->children);
            parse_tree_free(child);
        }

        free_parse_node(node);
    }
}


// Invoke callback for each pattern that matches 'value
void parse_tree_find(parse_node_t *node, const char *value,
                     parse_tree_visit_t *callback, void *handle)
{
    token_iterator_t t_iter;

    token_iterator_init(&t_iter, value, node->token_sep);
    parse_node_find(node, &t_iter, callback, handle);
}


// returns old payload or NULL if new
void *parse_tree_add_pattern(parse_node_t *node,
                             const char *pattern,
                             void *payload)
{
    token_iterator_t key;
    void *rc = NULL;
    char *norm = normalize_pattern(strdup(pattern),
                                   node->token_sep);

    token_iterator_init(&key, norm, node->token_sep);
    rc = parse_node_add_pattern(node, &key, pattern, payload);
    free(norm);
    return rc;
}


// returns the payload void *
void *parse_tree_remove_pattern(parse_node_t *node,
                                const char *pattern)
{
    token_iterator_t key;
    void *rc = NULL;
    char *norm = normalize_pattern(strdup(pattern),
                                   node->token_sep);

    token_iterator_init(&key, norm, node->token_sep);
    rc = parse_node_remove_pattern(node, &key, pattern);
    free(norm);
    return rc;
}

