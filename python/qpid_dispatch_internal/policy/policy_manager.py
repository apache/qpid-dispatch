#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License
#

"""

"""

import json
import traceback
from policy_local import PolicyLocal
from ..dispatch import LogAdapter, LOG_INFO, LOG_TRACE, LOG_DEBUG, LOG_ERROR



"""
Entity implementing the glue between the policy engine and the rest of the system.
"""

class PolicyManager(object):
    """

    """

    def __init__(self, agent):
        """
        """
        self._agent = agent
        self._policy_local = PolicyLocal(self)
        self.log_adapter = LogAdapter("POLICY")

    def log(self, level, text):
        info = traceback.extract_stack(limit=2)[0] # Caller frame info
        self.log_adapter.log(level, text, info[0], info[1])

    def _log(self, level, text):
        info = traceback.extract_stack(limit=3)[0] # Caller's caller frame info
        self.log_adapter.log(level, text, info[0], info[1])

    def log_debug(self, text):
        self._log(LOG_DEBUG, text)

    def log_info(self, text):
        self._log(LOG_INFO, text)

    def log_trace(self, text):
        self._log(LOG_TRACE, text)

    def log_error(self, text):
        self._log(LOG_ERROR, text)

    def get_agent(self):
        return self._agent

    #
    # Management interface to create a ruleset
    #
    def create_ruleset(self, attributes):
        """
        Create named policy ruleset
        @param[in] attributes: from config
        """
        self._policy_local.create_ruleset(attributes)
        # TODO: Create stats

    #
    # Runtime query interface
    #
    def lookup_user(self, user, host, app, conn_name):
        """
        Lookup function called from C.
        Determine if a user on host accessing app through AMQP Open is allowed
        according to the policy access rules.
        If allowed then return the policy settings name
        @param[in] user connection authId
        @param[in] host connection remote host numeric IP address as string
        @param[in] app application user is accessing
        @return settings user-group name if allowed; "" if not allowed
        # Note: the upolicy[0] output is list of group names joined with '|'.
        TODO: handle the AccessStats
        """
        return self._policy_local.lookup_user(user, host, app, conn_name)

    def lookup_settings(self, appname, name, upolicy):
        """
        Given a settings name, return the aggregated policy blob.
        @param[in] appname: application user is accessing
        @param[in] name: user group name
        @param[out] upolicy: dict holding policy values - the settings blob
                    TODO: make this a c struct
        @return if allowed by policy
        # Note: the upolicy output is a non-nested dict with settings of interest
        # TODO: figure out decent defaults for upolicy settings that are undefined
        """
        return self._policy_local.lookup_settings(appname, name, upolicy)

#
#
#
def policy_lookup_user(mgr, user, host, app, conn_name):
    """
    Look up a user in the policy database
    Called by C code
    @param mgr:
    @param user:
    @param host:
    @param app:
    @param conn_name:
    @return:
    """
    return mgr.lookup_user(user, host, app, conn_name)
