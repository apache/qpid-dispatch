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
from policy_util import PolicyError, HostStruct, HostAddr
from copy import deepcopy

"""
Entity implementing the business logic of user connection/access policy.
"""

#
#
class PolicyKeys(object):
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
    KW_CONNECTION_GROUPS           = "connectionGroups"
    KW_CONNECTION_INGRESS_POLICIES = "connectionIngressPolicies"
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

    # What settings does a user get when allowed to connect but
    # not restricted by a user group?
    KW_DEFAULT_SETTINGS         = "default"

    # Config file separator character for two IP addresses in a range
    KC_CONFIG_IP_SEP            = "-"

    # Config file separator character for names in a list
    KC_CONFIG_LIST_SEP          = ","

    # user-to-group computed map in compiled ruleset
    RULESET_U2G_MAP             = "U2G"
#
#
class PolicyCompiler(object):
    """
    Validate incoming configuration for legal schema.
    - Warn about section options that go unused.
    - Disallow negative max connection numbers.
    - Check that connectionOrigins resolve to IP hosts
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
        PolicyKeys.KW_CONNECTION_GROUPS,
        PolicyKeys.KW_CONNECTION_INGRESS_POLICIES,
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
        error = ""
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
        Handle an connectionGroups submap.
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
        key = PolicyKeys.KW_CONNECTION_GROUPS
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
        policy_out[PolicyKeys.KW_SOURCES] = []
        policy_out[PolicyKeys.KW_TARGETS] = []

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
                val = [x.strip(' ') for x in val.split(PolicyKeys.KC_CONFIG_LIST_SEP)]
                # deduplicate address lists
                val = list(set(val))
                policy_out[key] = val
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
        policy_out[PolicyKeys.KW_CONNECTION_GROUPS] = {}
        policy_out[PolicyKeys.KW_CONNECTION_INGRESS_POLICIES] = {}
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
                         PolicyKeys.KW_CONNECTION_GROUPS,
                         PolicyKeys.KW_CONNECTION_INGRESS_POLICIES
                         ]:
                try:
                    if not type(val) is dict:
                        errors.append("Application '%s' option '%s' must be of type 'dict' but is '%s'" %
                                      (name, key, type(val)))
                        return False
                    if key == PolicyKeys.KW_CONNECTION_GROUPS:
                        # Conection groups are lists of IP addresses that need to be
                        # converted into binary structures for comparisons.
                        val_out = {}
                        if not self.compile_connection_groups(name, val, val_out, warnings, errors):
                            return False
                        policy_out[key] = {}
                        policy_out[key].update(val_out)
                    else:
                        # deduplicate connectionIngressPolicy and userGroups lists
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
        # Create user-to-group map for looling up user's group
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

        # Each ingress policy name references must exist in connection_groups
        for cipname, cip in policy_out[PolicyKeys.KW_CONNECTION_INGRESS_POLICIES].iteritems():
            for co in cip:
                if not co in policy_out[PolicyKeys.KW_CONNECTION_GROUPS]:
                    errors.append("Application '%s' connection ingress policy '%s' references connection group '%s' but that group does not exist"
                                  (name, cipname, co))
                    return False

        return True

class PolicyLocal(object):
    """
    The policy database.
    """

    def __init__(self):
        """
        Create instance
        @params folder: relative path from __file__ to conf file folder
        """
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

        # _policy_compiler is a function
        #  validates incoming policy and readies it for internal use
        self._policy_compiler = PolicyCompiler()

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
            print ("LogMe: Application '%s' has warnings: %s" %
                   (name, warnings))
        self.rulesetdb[name] = {}
        self.rulesetdb[name].update(candidate)
        # TODO: Create stats

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
        try:
            if not app in self.rulesetdb:
                # TODO: ("LogMe: no policy defined for application %s" % app)
                return ""

            ruleset = self.rulesetdb[app]
            # User in a group or default?
            if user in ruleset[PolicyKeys.RULESET_U2G_MAP]:
                usergroup = ruleset[PolicyKeys.RULESET_U2G_MAP][user]
            else:
                if ruleset[PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT]:
                    usergroup = PolicyKeys.KW_DEFAULT_SETTINGS
                else:
                    # User is not in a group and default is disallowed. So no go.
                    return ""
            # User in usergroup allowed to connect from host?
            if usergroup in ruleset[PolicyKeys.KW_CONNECTION_INGRESS_POLICIES]:
                # User's usergroup is restricted to connecting from a host
                # defined by the group's ingress policy
                allowed = False
                uhs = HostStruct(host)
                cglist = ruleset[PolicyKeys.KW_CONNECTION_INGRESS_POLICIES][usergroup]
                for cg in cglist:
                    for cohost in ruleset[PolicyKeys.KW_CONNECTION_GROUPS][cg]:
                        if cohost.match_bin(uhs):
                            allowed = True
                            break
                    if allowed:
                        break
            else:
                # User's usergroup has no ingress policy so allow
                allowed = True
            if not allowed:
                return ""

            # This user passes administrative approval.
            # TODO: Count connection limits and possibly deny

            # Return success
            return usergroup

        except Exception, e:
            #print str(e)
            #pdb.set_trace()
            return ""

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
        try:
            if not appname in self.rulesetdb:
                # TODO: ("LogMe: no policy defined for application %s" % app)
                return ""

            ruleset = self.rulesetdb[appname]

            if not name in ruleset[PolicyKeys.KW_SETTINGS]:
                # TODO: ("LogMe: no user group settings for application %s group %s" % (app, name))
                return ""

            upolicy.update(ruleset[PolicyKeys.KW_SETTINGS][name])
            return True
        except Exception, e:
            #print str(e)
            #pdb.set_trace()
            return ""

    def test_load_config(self):
        ruleset_str = '["policyAccessRuleset", {"applicationName": "photoserver","maxConnections": 50,"maxConnPerUser": 5,"maxConnPerHost": 20,"userGroups": {"anonymous":       "anonymous","users":           "u1, u2","paidsubscribers": "p1, p2","test":            "zeke, ynot","admin":           "alice, bob","superuser":       "ellen"},"connectionGroups": {"Ten18":     "10.18.0.0-10.18.255.255","EllensWS":  "72.135.2.9","TheLabs":   "10.48.0.0-10.48.255.255, 192.168.100.0-192.168.100.255","localhost": "127.0.0.1, ::1","TheWorld":  "*"},"connectionIngressPolicies": {"anonymous":       "TheWorld","users":           "TheWorld","paidsubscribers": "TheWorld","test":            "TheLabs","admin":           "Ten18, TheLabs, localhost","superuser":       "EllensWS, localhost"},"connectionAllowDefault": true,'
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
