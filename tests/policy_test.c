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

#include "policy.h"

#include "parse_tree.h"
#include "policy_internal.h"
#include "test_case.h"

#include <stdio.h>
#include <string.h>

static char *test_link_name_lookup(void *context)
{
    // DISPATCH-1011: approval specifications are now CSV concatenated 3-tuples:
    // (user-subst-code, prefix, suffix)
    // codes are 'a'bsent, 'p'refix, 's'uffix, 'e'mbedded, '*'wildcard
    // OLD: 'joe', NEW: 'a,joe,'

    // Degenerate blank names
    if (_qd_policy_approve_link_name("a", "a", ""))
	return "blank proposed name not rejected";
    if (_qd_policy_approve_link_name("a", "", "a"))
	return "blank allowed list not rejected";

    // Easy matches
    if (!_qd_policy_approve_link_name("", "a,joe,", "joe"))
        return "proposed link 'joe' should match allowed links 'joe' but does not";
    if (_qd_policy_approve_link_name("", "a,joe,", "joey"))
        return "proposed link 'joey' should not match allowed links 'joe' but does";

    // Wildcard matches
    if (!_qd_policy_approve_link_name("", "a,joe*,", "joey"))
        return "proposed link 'joey' should match allowed links 'joe*' but does not";
    if (!_qd_policy_approve_link_name("", "a,joe*,", "joezzzZZZ"))
        return "proposed link 'joezzzZZZ' should match allowed links 'joe*' but does not";
    if (!_qd_policy_approve_link_name("", "a,joe,,*,,", "joey"))
        return "proposed link 'joey' should match allowed links 'joe,*' but does not";

    // Deeper match
    if (!_qd_policy_approve_link_name("", "a,no1,,a,no2,,a,no3,,a,yes,,a,no4,", "yes"))
        return "proposed link 'yes' should match allowed links 'no1,no2,no3,yes,no4' but does not";

    // Deeeper match - triggers malloc/free internal handler
#define BIG_N 512
    char * bufp = (char *)malloc(BIG_N * 8 + 6 + 1);
    if (!bufp)
        return "failed to allocate buffer for large link name test";
    char * wp = bufp;
    int i;
    for (i=0; i<BIG_N; i++) {
        wp += sprintf(wp, "a,n%03d,,", i);
    }
    sprintf(wp, "a,yes,");
    if (!_qd_policy_approve_link_name("", bufp, "yes")) {
        free(bufp);
        return "proposed link 'yes' should match allowed large list but does not";
    }
    free(bufp);

    // Substitute a user name
    if (!_qd_policy_approve_link_name("chuck", "e,ab,xyz", "abchuckxyz"))
        return "proposed link 'abchuckxyz' should match allowed links with ${user} but does not";
    if (!_qd_policy_approve_link_name("chuck", "p,,xyz", "chuckxyz"))
        return "proposed link 'chuckxyz' should match allowed links with ${user} but does not";
    if (!_qd_policy_approve_link_name("chuck", "s,ab,", "abchuck"))
        return "proposed link 'abchuck' should match allowed links with ${user} but does not";
//    if (!_qd_policy_approve_link_name("em", "temp-${user}", "temp-em"))
//        return "proposed link 'temp-em' should match allowed links with ${user} but does not";

    // Combine user name and wildcard
    if (!_qd_policy_approve_link_name("chuck", "e,ab,*", "abchuckzyxw"))
        return "proposed link 'abchuckzyxw' should match allowed links with ${user}* but does not";

    return 0;
}


static char *test_link_name_tree_lookup(void *context)
{
    qd_parse_tree_t *node = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    void *payload = (void*)1;

    qd_parse_tree_add_pattern_str(node, "${user}.xyz", payload);

    if (!_qd_policy_approve_link_name_tree("chuck", "p,,.xyz", "chuck.xyz", node)) {
        qd_parse_tree_free(node);
        return "proposed link 'chuck.xyz' should tree-match allow links with ${user} but does not";
    }

    if (_qd_policy_approve_link_name_tree("chuck", "p,,.xyz", "chuck.xyz.ynot", node)) {
        qd_parse_tree_free(node);
        return "proposed link 'chuck.xyz.ynot' should not tree-match allow links with ${user} but does";
    }

    qd_parse_tree_add_pattern_str(node, "${user}.#", payload);

    if (!_qd_policy_approve_link_name_tree("motronic", "p,,.#", "motronic", node)) {
        qd_parse_tree_free(node);
        return "proposed link 'motronic' should tree-match allow links with ${user} but does not";
    }

    if (!_qd_policy_approve_link_name_tree("motronic", "p,,.#", "motronic.stubs.wobbler", node)) {
        qd_parse_tree_free(node);
        return "proposed link 'motronic.stubs.wobbler' should tree-match allow links with ${user} but does not";
    }

    qd_parse_tree_t *node2 = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    qd_parse_tree_add_pattern_str(node2, "abc.${user}", payload);

    if (!_qd_policy_approve_link_name_tree("chuck", "s,abc.,", "abc.chuck", node2)) {
        qd_parse_tree_free(node);
        qd_parse_tree_free(node2);
        return "proposed link 'abc.chuck' should tree-match allow links with ${user} but does not";
    }

    if (_qd_policy_approve_link_name_tree("chuck", "s,abc.,", "abc.ynot.chuck", node2)) {
        qd_parse_tree_free(node);
        qd_parse_tree_free(node2);
        return "proposed link 'abc.ynot.chuck' should not tree-match allow links with ${user} but does";
    }

    if (_qd_policy_approve_link_name_tree("chuck", "s,abc.,", "abc.achuck", node2)) {
        qd_parse_tree_free(node);
        qd_parse_tree_free(node2);
        return "proposed link 'abc.achuck' should not tree-match allow links with ${user} but does";
    }

    if (_qd_policy_approve_link_name_tree("chuckginormous", "s,abc.,", "abc.chuck", node2)) {
        qd_parse_tree_free(node);
        qd_parse_tree_free(node2);
        return "proposed link 'abc.chuck' should not tree-match allow links with ${user} but does";
    }

    qd_parse_tree_t *node3 = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    qd_parse_tree_add_pattern_str(node3, "${user}", payload);

    if (!_qd_policy_approve_link_name_tree("chuck", "p,,", "chuck", node3)) {
        qd_parse_tree_free(node);
        qd_parse_tree_free(node2);
        qd_parse_tree_free(node3);
        return "proposed link 'chuck' should tree-match allow links with ${user} but does not";
    }

    qd_parse_tree_free(node);
    qd_parse_tree_free(node2);
    qd_parse_tree_free(node3);
    
    return 0;
}


static char *test_link_name_csv_parser(void *context)
{
    char * result;

    result = qd_policy_compile_allowed_csv("ttt");
    if (!!strcmp(result, "a,ttt,")) {
        free(result);
        return "simple csv with no subst failed";
    }
    free(result);

    result = qd_policy_compile_allowed_csv("ttt,uuu,vvvv");
    if (!!strcmp(result, "a,ttt,,a,uuu,,a,vvvv,")) {
        free(result);
        return "moderate csv with no subst failed";
    }
    free(result);

    result = qd_policy_compile_allowed_csv("*");
    if (!!strcmp(result, "*,,")) {
        free(result);
        return "wildcard csv failed";
    }
    free(result);

    result = qd_policy_compile_allowed_csv("${user}-temp");
    if (!!strcmp(result, "p,,-temp")) {
        free(result);
        return "csv with prefix subst failed";
    }
    free(result);

    result = qd_policy_compile_allowed_csv("temp-${user}");
    if (!!strcmp(result, "s,temp-,")) {
        free(result);
        return "csv with suffix subst failed";
    }
    free(result);

    result = qd_policy_compile_allowed_csv("temp-${user}-home");
    if (!!strcmp(result, "e,temp-,-home")) {
        free(result);
        return "csv with embedded subst failed";
    }
    free(result);

    return 0;
}


int policy_tests(void)
{
    int result = 0;
    char *test_group = "policy_tests";

    TEST_CASE(test_link_name_lookup, 0);
    TEST_CASE(test_link_name_tree_lookup, 0);
    TEST_CASE(test_link_name_csv_parser, 0);

    return result;
}

