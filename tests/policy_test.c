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

#include "test_case.h"
#include <stdio.h>
#include <string.h>
#include "policy.h"
#include "policy_internal.h"

static char *test_link_name_lookup(void *context)
{
    // Degenerate blank names
    if (_qd_policy_approve_link_name("a", "a", ""))
	return "blank proposed name not rejected";
    if (_qd_policy_approve_link_name("a", "", "a"))
	return "blank allowed list not rejected";

    // Easy matches
    if (!_qd_policy_approve_link_name("", "joe", "joe"))
        return "proposed link 'joe' should match allowed links 'joe' but does not";
    if (_qd_policy_approve_link_name("", "joe", "joey"))
        return "proposed link 'joey' should not match allowed links 'joe' but does";

    // Wildcard matches
    if (!_qd_policy_approve_link_name("", "joe*", "joey"))
        return "proposed link 'joey' should match allowed links 'joe*' but does not";
    if (!_qd_policy_approve_link_name("", "joe*", "joezzzZZZ"))
        return "proposed link 'joezzzZZZ' should match allowed links 'joe*' but does not";
    if (!_qd_policy_approve_link_name("", "joe,*", "joey"))
        return "proposed link 'joey' should match allowed links 'joe,*' but does not";

    // Deeper match
    if (!_qd_policy_approve_link_name("", "no1,no2,no3,yes,no4", "yes"))
        return "proposed link 'yes' should match allowed links 'no1,no2,no3,yes,no4' but does not";

    // Deeeper match - triggers malloc/free internal handler
    char * bufp = (char *)malloc(512 * 5 + 6);
    char * wp = bufp;
    int i;
    for (i=0; i<512; i++) {
        wp += sprintf(wp, "n%03d,", i);
    }
    sprintf(wp, "yes");
    if (!_qd_policy_approve_link_name("", bufp, "yes")) {
        free(bufp);
        return "proposed link 'yes' should match allowed large list but does not";
    }
    free(bufp);

    // Substitute a user name
    if (!_qd_policy_approve_link_name("chuck", "ab${user}xyz", "abchuckxyz"))
        return "proposed link 'abchuckxyz' should match allowed links with ${user} but does not";
    if (!_qd_policy_approve_link_name("chuck", "${user}xyz", "chuckxyz"))
        return "proposed link 'chuckxyz' should match allowed links with ${user} but does not";
    if (!_qd_policy_approve_link_name("chuck", "ab${user}", "abchuck"))
        return "proposed link 'abchuck' should match allowed links with ${user} but does not";

    // Combine user name and wildcard
    if (!_qd_policy_approve_link_name("chuck", "ab${user}*", "abchuckzyxw"))
        return "proposed link 'abchuckzyxw' should match allowed links with ${user}* but does not";
    
    return 0;
}

int policy_tests(void)
{
    int result = 0;
    char *test_group = "policy_tests";

    TEST_CASE(test_link_name_lookup, 0);

    return result;
}

