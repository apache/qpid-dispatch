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
from policy_util import PolicyError, HostStruct, HostAddr, PolicyAppConnectionMgr

"""
Entity implementing the business logic of user connection/access policy.
"""

#
#
class PolicyKeys(object):
    """
    String constants
    """
    # Common key words
    KW_IGNORED_NAME             = "name"
    KW_IGNORED_IDENTITY         = "identity"
    KW_IGNORED_TYPE             = "type"
    KW_APPLICATION_NAME         = "applicationName"

    # Policy ruleset key words
    KW_MAXCONN                     = "maxConnections"
    KW_MAXCONNPERHOST              = "maxConnPerHost"
    KW_MAXCONNPERUSER              = "maxConnPerUser"
    KW_USER_GROUPS                 = "userGroups"
    KW_INGRESS_HOST_GROUPS         = "ingressHostGroups"
    KW_INGRESS_POLICIES            = "ingressPolicies"
    KW_CONNECTION_ALLOW_DEFAULT    = "connectionAllowDefault"
    KW_SETTINGS                    = "settings"

    # Policy settings key words
    KW_USER_GROUP_NAME          = "userGroupName"
    KW_MAX_FRAME_SIZE           = "maxFrameSize"
    KW_MAX_MESSAGE_SIZE         = "maxMessageSize"
    KW_MAX_SESSION_WINDOW       = "maxSessionWindow"
    KW_MAX_SESSIONS             = "maxSessions"
    KW_MAX_SENDERS              = "maxSenders"
    KW_MAX_RECEIVERS            = "maxReceivers"
    KW_ALLOW_DYNAMIC_SRC        = "allowDynamicSrc"
    KW_ALLOW_ANONYMOUS_SENDER   = "allowAnonymousSender"
    KW_SOURCES                  = "sources"
    KW_TARGETS                  = "targets"

    # Policy stats key words
    KW_CONNECTIONS_APPROVED     = "connectionsApproved"
    KW_CONNECTIONS_DENIED       = "connectionsDenied"
    KW_CONNECTIONS_CURRENT      = "connectionsCurrent"
    KW_PER_USER_STATE           = "perUserState"
    KW_PER_HOST_STATE           = "perHostState"

    # What settings does a user get when allowed to connect but
    # not restricted by a user group?
    KW_DEFAULT_SETTINGS         = "default"

    # Config file separator character for two IP addresses in a range
    KC_CONFIG_IP_SEP            = "-"

    # Config file separator character for names in a list
    KC_CONFIG_LIST_SEP          = ","

    # user-to-group computed map in compiled ruleset
    RULESET_U2G_MAP             = "U2G"

    # policy stats controlled by C code but referenced by settings
    KW_CSTATS                   = "denialCounts"
#
#
class PolicyCompiler(object):
    """
    Validate incoming configuration for legal schema.
    - Warn about section options that go unused.
    - Disallow negative max connection numbers.
    - Check that connectionOrigins resolve to IP hosts.
    - Enforce internal consistency,
    """

    allowed_ruleset_options = [
        PolicyKeys.KW_IGNORED_NAME,
        PolicyKeys.KW_IGNORED_IDENTITY,
        PolicyKeys.KW_IGNORED_TYPE,
        PolicyKeys.KW_APPLICATION_NAME,
        PolicyKeys.KW_MAXCONN,
        PolicyKeys.KW_MAXCONNPERHOST,
        PolicyKeys.KW_MAXCONNPERUSER,
        PolicyKeys.KW_USER_GROUPS,
        PolicyKeys.KW_INGRESS_HOST_GROUPS,
        PolicyKeys.KW_INGRESS_POLICIES,
        PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT,
        PolicyKeys.KW_SETTINGS
        ]

    allowed_settings_options = [
        PolicyKeys.KW_MAX_FRAME_SIZE,
        PolicyKeys.KW_MAX_MESSAGE_SIZE,
        PolicyKeys.KW_MAX_SESSION_WINDOW,
        PolicyKeys.KW_MAX_SESSIONS,
        PolicyKeys.KW_MAX_SENDERS,
        PolicyKeys.KW_MAX_RECEIVERS,
        PolicyKeys.KW_ALLOW_DYNAMIC_SRC,
        PolicyKeys.KW_ALLOW_ANONYMOUS_SENDER,
        PolicyKeys.KW_SOURCES,
        PolicyKeys.KW_TARGETS
        ]

    def __init__(self):
        """
        Create a validator
        """
        pass


    def validateNumber(self, val, v_min, v_max, errors):
        """
        Range check a numeric int policy value
        @param[in] val policy value to check
        @param[in] v_min minumum value
        @param[in] v_max maximum value. zero disables check
        @param[out] errors failure message
        @return v_min <= val <= v_max
        """
        try:
            v_int = int(val)
        except Exception, e:
            errors.append("Value '%s' does not resolve to an integer." % val)
            return False
        if v_int < v_min:
            errors.append("Value '%s' is below minimum '%s'." % (val, v_min))
            return False
        if v_max > 0 and v_int > v_max:
            errors.append("Value '%s' is above maximum '%s'." % (val, v_max))
            return False
        return True


    def compile_connection_groups(self, name, submap_in, submap_out, warnings, errors):
        """
        Handle an ingressHostGroups submap.
        Each origin value is verified. On a successful run the submap
        is replaced parsed lists of HostAddr objects.
        @param[in] name application name
        @param[in] submap_in user input origin list as text strings
        @param[out] submap_out user inputs replaced with HostAddr objects
        @param[out] warnings nonfatal irregularities observed
        @param[out] errors descriptions of failure
        @return - origins is usable. If True then warnings[] may contain useful
                  information about fields that are ignored. If False then
                  warnings[] may contain info and errors[0] will hold the
                  description of why the origin was rejected.
        """
        key = PolicyKeys.KW_INGRESS_HOST_GROUPS
        for coname in submap_in:
            try:
                ostr = str(submap_in[coname])
                olist = [x.strip(' ') for x in ostr.split(PolicyKeys.KC_CONFIG_LIST_SEP)]
                submap_out[coname] = []
                for co in olist:
                    coha = HostAddr(co, PolicyKeys.KC_CONFIG_IP_SEP)
                    submap_out[coname].append(coha)
            except Exception, e:
                errors.append("Application '%s' option '%s' connectionOption '%s' failed to translate: '%s'." %
                                (name, key, coname, e))
                return False
        return True


    def compile_app_settings(self, appname, usergroup, policy_in, policy_out, warnings, errors):
        """
        Compile a schema from processed json format to local internal format.
        @param[in] name application name
        @param[in] policy_in user config settings
        @param[out] policy_out validated Internal format
        @param[out] warnings nonfatal irregularities observed
        @param[out] errors descriptions of failure
        @return - settings are usable. If True then warnings[] may contain useful
                  information about fields that are ignored. If False then
                  warnings[] may contain info and errors[0] will hold the
                  description of why the policy was rejected.
        """
        # rulesets may not come through standard config so make nice defaults
        policy_out[PolicyKeys.KW_MAX_FRAME_SIZE] = 65536
        policy_out[PolicyKeys.KW_MAX_MESSAGE_SIZE] = 0
        policy_out[PolicyKeys.KW_MAX_SESSION_WINDOW] = 2147483647
        policy_out[PolicyKeys.KW_MAX_SESSIONS] = 10
        policy_out[PolicyKeys.KW_MAX_SENDERS] = 10
        policy_out[PolicyKeys.KW_MAX_RECEIVERS] = 10
        policy_out[PolicyKeys.KW_ALLOW_DYNAMIC_SRC] = False
        policy_out[PolicyKeys.KW_ALLOW_ANONYMOUS_SENDER] = False
        policy_out[PolicyKeys.KW_SOURCES] = ''
        policy_out[PolicyKeys.KW_TARGETS] = ''

        cerror = []
        for key, val in policy_in.iteritems():
            if key not in self.allowed_settings_options:
                warnings.append("Application '%s' user group '%s' option '%s' is ignored." %
                                (appname, usergroup, key))
            if key in [PolicyKeys.KW_MAX_FRAME_SIZE,
                       PolicyKeys.KW_MAX_MESSAGE_SIZE,
                       PolicyKeys.KW_MAX_RECEIVERS,
                       PolicyKeys.KW_MAX_SENDERS,
                       PolicyKeys.KW_MAX_SESSION_WINDOW,
                       PolicyKeys.KW_MAX_SESSIONS
                       ]:
                if not self.validateNumber(val, 0, 0, cerror):
                    errors.append("Application '%s' user group '%s' option '%s' has error '%s'." %
                                  (appname, usergroup, key, cerror[0]))
                    return False
                policy_out[key] = val
            elif key in [PolicyKeys.KW_ALLOW_ANONYMOUS_SENDER,
                         PolicyKeys.KW_ALLOW_DYNAMIC_SRC
                         ]:
                if not type(val) is bool:
                    errors.append("Application '%s' user group '%s' option '%s' has illegal boolean value '%s'." %
                                  (appname, usergroup, key, val))
                    return False
                policy_out[key] = val
            elif key in [PolicyKeys.KW_SOURCES,
                         PolicyKeys.KW_TARGETS
                         ]:
                # accept a string or list
                if type(val) is str:
                    # 'abc, def, mytarget'
                    val = [x.strip(' ') for x in val.split(PolicyKeys.KC_CONFIG_LIST_SEP)]
                elif type(val) is list:
                    # ['abc', 'def', 'mytarget']
                    pass
                elif type(val) is unicode:
                    # u'abc, def, mytarget'
                    val = [x.strip(' ') for x in str(val).split(PolicyKeys.KC_CONFIG_LIST_SEP)]
                else:
                    errors.append("Application '%s' user group '%s' option '%s' has illegal value '%s'. Type must be 'str' or 'list' but is '%s;" %
                                  (appname, usergroup, key, val, type(val)))
                # deduplicate address lists
                val = list(set(val))
                # output result is CSV string with no white space between values: 'abc,def,mytarget'
                policy_out[key] = ','.join(val)
        return True


    def compile_access_ruleset(self, name, policy_in, policy_out, warnings, errors):
        """
        Compile a schema from processed json format to local internal format.
        @param[in] name application name
        @param[in] policy_in raw policy to be validated
        @param[out] policy_out validated Internal format
        @param[out] warnings nonfatal irregularities observed
        @param[out] errors descriptions of failure
        @return - policy is usable. If True then warnings[] may contain useful
                  information about fields that are ignored. If False then
                  warnings[] may contain info and errors[0] will hold the
                  description of why the policy was rejected.
        """
        cerror = []
        # rulesets may not come through standard config so make nice defaults
        policy_out[PolicyKeys.KW_MAXCONN] = 0
        policy_out[PolicyKeys.KW_MAXCONNPERHOST] = 0
        policy_out[PolicyKeys.KW_MAXCONNPERUSER] = 0
        policy_out[PolicyKeys.KW_USER_GROUPS] = {}
        policy_out[PolicyKeys.KW_INGRESS_HOST_GROUPS] = {}
        policy_out[PolicyKeys.KW_INGRESS_POLICIES] = {}
        policy_out[PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT] = False
        policy_out[PolicyKeys.KW_SETTINGS] = {}

        # validate the options
        for key, val in policy_in.iteritems():
            if key not in self.allowed_ruleset_options:
                warnings.append("Application '%s' option '%s' is ignored." %
                                (name, key))
            if key in [PolicyKeys.KW_MAXCONN,
                       PolicyKeys.KW_MAXCONNPERHOST,
                       PolicyKeys.KW_MAXCONNPERUSER
                       ]:
                if not self.validateNumber(val, 0, 65535, cerror):
                    msg = ("Application '%s' option '%s' has error '%s'." % 
                           (name, key, cerror[0]))
                    errors.append(msg)
                    return False
                policy_out[key] = val
            elif key in [PolicyKeys.KW_USER_GROUPS,
                         PolicyKeys.KW_INGRESS_HOST_GROUPS,
                         PolicyKeys.KW_INGRESS_POLICIES
                         ]:
                try:
                    if not type(val) is dict:
                        errors.append("Application '%s' option '%s' must be of type 'dict' but is '%s'" %
                                      (name, key, type(val)))
                        return False
                    if key == PolicyKeys.KW_INGRESS_HOST_GROUPS:
                        # Conection groups are lists of IP addresses that need to be
                        # converted into binary structures for comparisons.
                        val_out = {}
                        if not self.compile_connection_groups(name, val, val_out, warnings, errors):
                            return False
                        policy_out[key] = {}
                        policy_out[key].update(val_out)
                    else:
                        # deduplicate ingressPolicy and userGroups lists
                        for k,v in val.iteritems():
                            v = [x.strip(' ') for x in v.split(PolicyKeys.KC_CONFIG_LIST_SEP)]
                            v = list(set(v))
                            val[k] = v
                        policy_out[key] = val
                except Exception, e:
                    errors.append("Application '%s' option '%s' error processing map: %s" %
                                  (name, key, e))
                    return False
            elif key in [PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT]:
                if not type(val) is bool:
                    errors.append("Application '%s' option '%s' must be of type 'bool' but is '%s'" %
                                  (name, key, type(val)))
                    return False
                policy_out[key] = val
            elif key in [PolicyKeys.KW_SETTINGS]:
                if not type(val) is dict:
                    errors.append("Application '%s' option '%s' must be of type 'dict' but is '%s'" %
                                  (name, key, type(val)))
                    return False
                for skey, sval in val.iteritems():
                    newsettings = {}
                    if not self.compile_app_settings(name, skey, sval, newsettings, warnings, errors):
                        return False
                    policy_out[key][skey] = {}
                    policy_out[key][skey].update(newsettings)

        # Verify that each user is in only one group.
        # Verify that each user group has defined settings
        # Create user-to-group map for looking up user's group
        policy_out[PolicyKeys.RULESET_U2G_MAP] = {}
        if PolicyKeys.KW_USER_GROUPS in policy_out:
            for group, userlist in policy_out[PolicyKeys.KW_USER_GROUPS].iteritems():
                for user in userlist:
                    if user in policy_out[PolicyKeys.RULESET_U2G_MAP]:
                        errors.append("Application '%s' user '%s' is in multiple user groups '%s' and '%s'" %
                                      (name, user, policy_out[PolicyKeys.RULESET_U2G_MAP][user], group))
                        return False
                    else:
                        policy_out[PolicyKeys.RULESET_U2G_MAP][user] = group
                if not group in policy_out[PolicyKeys.KW_SETTINGS]:
                    errors.append("Application '%s' user group '%s' has no defined settings" %
                                  (name, group))
                    return False

        # Default connections require a default settings
        if policy_out[PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT]:
            if not PolicyKeys.KW_DEFAULT_SETTINGS in policy_out[PolicyKeys.KW_SETTINGS]:
                errors.append("Application '%s' allows connections by default but default settings are not defined" %
                              (name))
                return False

        # Each ingress policy name reference must exist in ingressHostGroups
        for cipname, cip in policy_out[PolicyKeys.KW_INGRESS_POLICIES].iteritems():
            for co in cip:
                if not co in policy_out[PolicyKeys.KW_INGRESS_HOST_GROUPS]:
                    errors.append("Application '%s' connection ingress policy '%s' references ingress host group '%s' but that group does not exist"
                                  (name, cipname, co))
                    return False

        return True


#
#
class AppStats(object):
    """
    Maintain live state and statistics for an application.
    """
    def __init__(self, id, manager, ruleset):
        self.my_id = id
        self._manager = manager
        self.conn_mgr = PolicyAppConnectionMgr(
                ruleset[PolicyKeys.KW_MAXCONN],
                ruleset[PolicyKeys.KW_MAXCONNPERUSER],
                ruleset[PolicyKeys.KW_MAXCONNPERHOST])
        self._cstats = self._manager.get_agent().qd.qd_dispatch_policy_c_counts_alloc()
        self._manager.get_agent().add_implementation(self, "policyStats")

    def update_ruleset(self, ruleset):
        """
        The parent ruleset has changed.
        Propagate settings into the connection manager.
        @param ruleset: new ruleset
        @return:
        """
        self.conn_mgr.update(
            ruleset[PolicyKeys.KW_MAXCONN],
            ruleset[PolicyKeys.KW_MAXCONNPERHOST],
            ruleset[PolicyKeys.KW_MAXCONNPERUSER])

    def refresh_entity(self, attributes):
        """Refresh management attributes"""
        entitymap = {}
        entitymap[PolicyKeys.KW_APPLICATION_NAME] =     self.my_id
        entitymap[PolicyKeys.KW_CONNECTIONS_APPROVED] = self.conn_mgr.connections_approved
        entitymap[PolicyKeys.KW_CONNECTIONS_DENIED] =   self.conn_mgr.connections_denied
        entitymap[PolicyKeys.KW_CONNECTIONS_CURRENT] =  self.conn_mgr.connections_active
        entitymap[PolicyKeys.KW_PER_USER_STATE] =       self.conn_mgr.per_user_state
        entitymap[PolicyKeys.KW_PER_HOST_STATE] =       self.conn_mgr.per_host_state
        self._manager.get_agent().qd.qd_dispatch_policy_c_counts_refresh(self._cstats, entitymap)
        attributes.update(entitymap)

    def can_connect(self, conn_id, user, host, diags):
        return self.conn_mgr.can_connect(conn_id, user, host, diags)

    def disconnect(self, conn_id, user, host):
        self.conn_mgr.disconnect(conn_id, user, host)

    def count_other_denial(self):
        self.conn_mgr.count_other_denial()

    def get_cstats(self):
        return self._cstats

#
#
class ConnectionFacts:
    def __init__(self, user, host, app, conn_name):
        self.user = user
        self.host = host
        self.app = app
        self.conn_name = conn_name

#
#
class PolicyLocal(object):
    """
    The local policy database.
    """

    def __init__(self, manager):
        """
        Create instance
        @params manager policy manager class
        """
        # manager is a class
        #  It provides access the dispatch system functions
        self._manager = manager

        # rulesetdb is a map
        #  key : application name
        #  val : ruleset for this app
        # created by configuration
        # augmented by policy compiler
        self.rulesetdb = {}

        # settingsdb is a map
        #  key : <application name>
        #  val : a map
        #   key : <user group name>
        #   val : settings to use for user's connection
        # created by configuration
        self.settingsdb = {}

        # statsdb is a map
        #  key : <application name>
        #  val : AppStats object
        self.statsdb = {}

        # _policy_compiler is a function
        #  validates incoming policy and readies it for internal use
        self._policy_compiler = PolicyCompiler()

        # _connections is a map
        #  key : numeric connection id
        #  val : ConnectionFacts
        # Entries created as connection AMQP Opens arrive
        # Entries destroyed as sockets closed
        self._connections = {}

        # _default_application is a string
        #  holds the name of the policyRuleset to use when the
        #  open.hostname is not found in the rulesetdb
        self._default_application = ""

        # _default_application_enabled is a boolean
        #  controls default application fallback logic
        self._default_application_enabled = False

    #
    # Service interfaces
    #
    def create_ruleset(self, attributes):
        """
        Create named policy ruleset
        @param[in] attributes: from config
        """
        warnings = []
        diag = []
        candidate = {}
        name = attributes[PolicyKeys.KW_APPLICATION_NAME]
        result = self._policy_compiler.compile_access_ruleset(name, attributes, candidate, warnings, diag)
        if not result:
            raise PolicyError( "Policy '%s' is invalid: %s" % (name, diag[0]) )
        if len(warnings) > 0:
            for warning in warnings:
                self._manager.log_warning(warning)
        if name not in self.rulesetdb:
            self.statsdb[name] = AppStats(name, self._manager, candidate)
            self._manager.log_info("Created policy rules for application %s" % name)
        else:
            self.statsdb[name].update_ruleset(candidate)
            self._manager.log_info("Updated policy rules for application %s" % name)
        self.rulesetdb[name] = {}
        self.rulesetdb[name].update(candidate)

    def policy_read(self, name):
        """
        Read policy for named application
        @param[in] name application name
        @return policy data in raw user format
        """
        return self.rulesetdb[name]

    def policy_update(self, name, policy):
        """
        Update named policy
        @param[in] name application name
        @param[in] policy data in raw user input
        """
        if not name in self.rulesetdb:
            raise PolicyError("Policy '%s' does not exist" % name)
        self.policy_create(name, policy)

    def policy_delete(self, name):
        """
        Delete named policy
        @param[in] name application name
        """
        if not name in self.rulesetdb:
            raise PolicyError("Policy '%s' does not exist" % name)
        del self.rulesetdb[name]

    #
    # db enumerator
    #
    def policy_db_get_names(self):
        """
        Return a list of application names in this policy
        """
        return self.rulesetdb.keys()

    def set_default_application(self, name, enabled):
        """
        Set the default application name and control its enablement.
        Raise PolicyError if the named application is enabled but absent.
        @param name: the name of the default application
        @param enabled: default application ruleset logic is active
        @return: none
        """
        self._default_application = name
        self._default_application_enabled = enabled
        if enabled:
            if not name in self.policy_db_get_names():
                raise PolicyError("Policy fallback defaultApplication '%s' does not exist" % name)
            self._manager.log_info("Policy fallback defaultApplication is enabled: '%s'" % name)
        else:
            self._manager.log_info("Policy fallback defaultApplication is disabled")

    #
    # Runtime query interface
    #
    def lookup_user(self, user, host, app_in, conn_name, conn_id):
        """
        Lookup function called from C.
        Determine if a user on host accessing app through AMQP Open is allowed
        according to the policy access rules.
        If allowed then return the policy settings name. If stats.can_connect
        returns true then it has registered and counted the connection.
        @param[in] user connection authId
        @param[in] host connection remote host numeric IP address as string
        @param[in] app_in application user is accessing
        @param[in] conn_name connection name used for tracking reports
        @param[in] conn_id internal connection id
        @return settings user-group name if allowed; "" if not allowed
        """
        try:
            app = app_in
            if not app_in in self.rulesetdb:
                if self._default_application_enabled:
                    app = self._default_application
                else:
                    self._manager.log_info(
                        "DENY AMQP Open for user '%s', host '%s', application '%s': "
                        "No policy defined for application" % (user, host, app))
                    return ""

            ruleset = self.rulesetdb[app]

            if not app in self.statsdb:
                msg = (
                    "DENY AMQP Open for user '%s', host '%s', application '%s': "
                    "INTERNAL: Policy is defined but stats are missing" % (user, host, app))
                raise PolicyError(msg)
            stats = self.statsdb[app]
            # User in a group or default?
            if user in ruleset[PolicyKeys.RULESET_U2G_MAP]:
                usergroup = ruleset[PolicyKeys.RULESET_U2G_MAP][user]
            else:
                if ruleset[PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT]:
                    usergroup = PolicyKeys.KW_DEFAULT_SETTINGS
                else:
                    self._manager.log_info(
                        "DENY AMQP Open for user '%s', host '%s', application '%s': "
                        "User is not in a user group and default users are denied" % (user, host, app))
                    stats.count_other_denial()
                    return ""
            # User in usergroup allowed to connect from host?
            if usergroup in ruleset[PolicyKeys.KW_INGRESS_POLICIES]:
                # User's usergroup is restricted to connecting from a host
                # defined by the group's ingress policy
                allowed = False
                uhs = HostStruct(host)
                cglist = ruleset[PolicyKeys.KW_INGRESS_POLICIES][usergroup]
                for cg in cglist:
                    for cohost in ruleset[PolicyKeys.KW_INGRESS_HOST_GROUPS][cg]:
                        if cohost.match_bin(uhs):
                            allowed = True
                            break
                    if allowed:
                        break
            else:
                # User's usergroup has no ingress policy so allow
                allowed = True
            if not allowed:
                self._manager.log_info(
                    "DENY AMQP Open for user '%s', host '%s', application '%s': "
                    "User is not allowed to connect from this network host" % (user, host, app))
                stats.count_other_denial()
                return ""

            # This user passes administrative approval.
            # Now check live connection counts
            diags = []
            if not stats.can_connect(conn_name, user, host, diags):
                for diag in diags:
                    self._manager.log_info(
                        "DENY AMQP Open for user '%s', host '%s', application '%s': "
                        "%s" % (user, host, app, diag))
                return ""

            # Record facts about this connection to use during teardown
            facts = ConnectionFacts(user, host, app, conn_name)
            self._connections[conn_id] = facts

            # Return success
            return usergroup

        except Exception, e:
            self._manager.log_info(
                "DENY AMQP Open lookup_user failed for user '%s', host '%s', application '%s': "
                "Internal error: %s" % (user, host, app, e))
            # return failure
            return ""

    def lookup_settings(self, appname_in, name, upolicy):
        """
        Given a settings name, return the aggregated policy blob.
        @param[in] appname: application user is accessing
        @param[in] name: user group name
        @param[out] upolicy: dict holding policy values - the settings blob
                    TODO: make this a c struct
        @return if lookup worked
        # Note: the upolicy output is a non-nested dict with settings of interest
        """
        try:
            appname = appname_in
            if not appname in self.rulesetdb and self._default_application_enabled:
                appname = self._default_application

            if not appname in self.rulesetdb:
                self._manager.log_info(
                        "lookup_settings fail for application '%s', user group '%s': "
                        "No policy defined for this application" % (appname, name))
                return False

            ruleset = self.rulesetdb[appname]

            if not name in ruleset[PolicyKeys.KW_SETTINGS]:
                self._manager.log_trace(
                        "lookup_settings fail for application '%s', user group '%s': "
                        "This application has no settings for the user group" % (appname, name))
                return False

            upolicy.update(ruleset[PolicyKeys.KW_SETTINGS][name])
            upolicy[PolicyKeys.KW_CSTATS] = self.statsdb[appname].get_cstats()
            return True
        except Exception, e:
            return False

    def close_connection(self, conn_id):
        """
        Close the connection.
        @param conn_id:
        @return:
        """
        try:
            if conn_id in self._connections:
                facts = self._connections[conn_id]
                stats = self.statsdb[facts.app]
                stats.disconnect(facts.conn_name, facts.user, facts.host)
                del self._connections[conn_id]
        except Exception, e:
            self._manager.log_trace(
                "Policy internal error closing connection id %s. %s" % (conn_id, str(e)))

    #
    #
    def test_load_config(self):
        """
        Test function to load a policy.
        @return:
        """
        ruleset_str = '["policyAccessRuleset", {"applicationName": "photoserver","maxConnections": 50,"maxConnPerUser": 5,"maxConnPerHost": 20,"userGroups": {"anonymous":       "anonymous","users":           "u1, u2","paidsubscribers": "p1, p2","test":            "zeke, ynot","admin":           "alice, bob","superuser":       "ellen"},"ingressHostGroups": {"Ten18":     "10.18.0.0-10.18.255.255","EllensWS":  "72.135.2.9","TheLabs":   "10.48.0.0-10.48.255.255, 192.168.100.0-192.168.100.255","localhost": "127.0.0.1, ::1","TheWorld":  "*"},"ingressPolicies": {"anonymous":       "TheWorld","users":           "TheWorld","paidsubscribers": "TheWorld","test":            "TheLabs","admin":           "Ten18, TheLabs, localhost","superuser":       "EllensWS, localhost"},"connectionAllowDefault": true,'
        ruleset_str += '"settings": {'
        ruleset_str += '"anonymous":      {"maxFrameSize": 111111,"maxMessageSize":   111111,"maxSessionWindow": 111111,"maxSessions":           1,"maxSenders":           11,"maxReceivers":         11,"allowDynamicSrc":      false,"allowAnonymousSender": false,"sources": "public",                           "targets": ""},'
        ruleset_str += '"users":          {"maxFrameSize": 222222,"maxMessageSize":   222222,"maxSessionWindow": 222222,"maxSessions":           2,"maxSenders":           22,"maxReceivers":         22,"allowDynamicSrc":      false,"allowAnonymousSender": false,"sources": "public, private",                  "targets": "public"},'
        ruleset_str += '"paidsubscribers":{"maxFrameSize": 333333,"maxMessageSize":   333333,"maxSessionWindow": 333333,"maxSessions":           3,"maxSenders":           33,"maxReceivers":         33,"allowDynamicSrc":      true, "allowAnonymousSender": false,"sources": "public, private",                  "targets": "public, private"},'
        ruleset_str += '"test":           {"maxFrameSize": 444444,"maxMessageSize":   444444,"maxSessionWindow": 444444,"maxSessions":           4,"maxSenders":           44,"maxReceivers":         44,"allowDynamicSrc":      true, "allowAnonymousSender": true, "sources": "private",                          "targets": "private"},'
        ruleset_str += '"admin":          {"maxFrameSize": 555555,"maxMessageSize":   555555,"maxSessionWindow": 555555,"maxSessions":           5,"maxSenders":           55,"maxReceivers":         55,"allowDynamicSrc":      true, "allowAnonymousSender": true, "sources": "public, private, management",      "targets": "public, private, management"},'
        ruleset_str += '"superuser":      {"maxFrameSize": 666666,"maxMessageSize":   666666,"maxSessionWindow": 666666,"maxSessions":           6,"maxSenders":           66,"maxReceivers":         66,"allowDynamicSrc":      false,"allowAnonymousSender": false,"sources": "public, private, management, root","targets": "public, private, management, root"},'
        ruleset_str += '"default":        {"maxFrameSize": 222222,"maxMessageSize":   222222,"maxSessionWindow": 222222,"maxSessions":           2,"maxSenders":           22,"maxReceivers":         22,"allowDynamicSrc":      false,"allowAnonymousSender": false,"sources": "public, private",                  "targets": "public"}'
        ruleset_str += '}}]'
        ruleset = json.loads(ruleset_str)

        self.create_ruleset(ruleset[1])
