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

"""Entity implementing the business logic of user connection/access policy."""

import json
from .policy_util import PolicyError, HostStruct, HostAddr, PolicyAppConnectionMgr, is_ipv6_enabled


class PolicyKeys:
    """
    String constants
    """
    # Common key words
    KW_IGNORED_NAME             = "name"
    KW_IGNORED_IDENTITY         = "identity"
    KW_IGNORED_TYPE             = "type"
    KW_VHOST_NAME               = "hostname"
    KW_VHOST_DEPRECATED_ID      = "id"

    # Policy ruleset key words
    KW_MAXCONN                     = "maxConnections"
    KW_MAXCONNPERHOST              = "maxConnectionsPerHost"
    KW_MAXCONNPERUSER              = "maxConnectionsPerUser"
    KW_CONNECTION_ALLOW_DEFAULT    = "allowUnknownUser"
    KW_GROUPS                      = "groups"

    # Policy settings key words
    KW_USERS                     = "users"
    KW_REMOTE_HOSTS              = "remoteHosts"
    KW_MAX_FRAME_SIZE            = "maxFrameSize"
    KW_MAX_MESSAGE_SIZE          = "maxMessageSize"
    KW_MAX_SESSION_WINDOW        = "maxSessionWindow"
    KW_MAX_SESSIONS              = "maxSessions"
    KW_MAX_SENDERS               = "maxSenders"
    KW_MAX_RECEIVERS             = "maxReceivers"
    KW_ALLOW_DYNAMIC_SRC         = "allowDynamicSource"
    KW_ALLOW_ANONYMOUS_SENDER    = "allowAnonymousSender"
    KW_ALLOW_USERID_PROXY        = "allowUserIdProxy"
    KW_ALLOW_WAYPOINT_LINKS      = "allowWaypointLinks"
    KW_ALLOW_FALLBACK_LINKS      = "allowFallbackLinks"
    KW_ALLOW_DYNAMIC_LINK_ROUTES = "allowDynamicLinkRoutes"
    KW_ALLOW_ADMIN_STATUS_UPDATE = "allowAdminStatusUpdate"
    KW_SOURCES                   = "sources"
    KW_TARGETS                   = "targets"
    KW_SOURCE_PATTERN            = "sourcePattern"
    KW_TARGET_PATTERN            = "targetPattern"
    KW_VHOST_ALIASES             = "aliases"

    # Policy stats key words
    KW_CONNECTIONS_APPROVED     = "connectionsApproved"
    KW_CONNECTIONS_DENIED       = "connectionsDenied"
    KW_CONNECTIONS_CURRENT      = "connectionsCurrent"
    KW_LINKS_DENIED             = "linksDenied"
    KW_TOTAL_DENIALS            = "totalDenials"
    KW_PER_USER_STATE           = "perUserState"
    KW_PER_HOST_STATE           = "perHostState"

    # What settings does a user get when allowed to connect but
    # not restricted by a user group?
    KW_DEFAULT_SETTINGS         = "$default"

    # Config file separator character for two IP addresses in a range
    KC_CONFIG_IP_SEP            = "-"

    # Config file separator character for names in a list
    KC_CONFIG_LIST_SEP          = ","

    # user-to-group computed map in compiled ruleset
    RULESET_U2G_MAP             = "U2G"

    # policy stats controlled by C code but referenced by settings
    KW_CSTATS                   = "denialCounts"

    # Username subsitituion token in link source and target names and patterns
    KC_TOKEN_USER               = "${user}"

    # Link target/source name wildcard tuple keys
    KC_TUPLE_ABSENT             = 'a'
    KC_TUPLE_PREFIX             = 'p'
    KC_TUPLE_SUFFIX             = 's'
    KC_TUPLE_EMBED              = 'e'
    KC_TUPLE_WILDCARD           = '*'

#
#


class PolicyCompiler:
    """
    Validate incoming configuration for legal schema.
    - Warn about section options that go unused.
    - Disallow negative max connection/message size numbers.
    - Check that connectionOrigins resolve to IP hosts.
    - Enforce internal consistency,
    """

    allowed_ruleset_options = [
        PolicyKeys.KW_IGNORED_NAME,
        PolicyKeys.KW_IGNORED_IDENTITY,
        PolicyKeys.KW_IGNORED_TYPE,
        PolicyKeys.KW_VHOST_NAME,
        PolicyKeys.KW_MAXCONN,
        PolicyKeys.KW_MAX_MESSAGE_SIZE,
        PolicyKeys.KW_MAXCONNPERHOST,
        PolicyKeys.KW_MAXCONNPERUSER,
        PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT,
        PolicyKeys.KW_GROUPS,
        PolicyKeys.KW_VHOST_ALIASES
    ]

    allowed_settings_options = [
        PolicyKeys.KW_USERS,
        PolicyKeys.KW_REMOTE_HOSTS,
        PolicyKeys.KW_MAXCONNPERHOST,
        PolicyKeys.KW_MAXCONNPERUSER,
        PolicyKeys.KW_MAX_FRAME_SIZE,
        PolicyKeys.KW_MAX_MESSAGE_SIZE,
        PolicyKeys.KW_MAX_SESSION_WINDOW,
        PolicyKeys.KW_MAX_SESSIONS,
        PolicyKeys.KW_MAX_SENDERS,
        PolicyKeys.KW_MAX_RECEIVERS,
        PolicyKeys.KW_ALLOW_DYNAMIC_SRC,
        PolicyKeys.KW_ALLOW_ANONYMOUS_SENDER,
        PolicyKeys.KW_ALLOW_USERID_PROXY,
        PolicyKeys.KW_ALLOW_WAYPOINT_LINKS,
        PolicyKeys.KW_ALLOW_FALLBACK_LINKS,
        PolicyKeys.KW_ALLOW_DYNAMIC_LINK_ROUTES,
        PolicyKeys.KW_ALLOW_ADMIN_STATUS_UPDATE,
        PolicyKeys.KW_SOURCES,
        PolicyKeys.KW_TARGETS,
        PolicyKeys.KW_SOURCE_PATTERN,
        PolicyKeys.KW_TARGET_PATTERN
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
        except Exception as e:
            errors.append("Value '%s' does not resolve to an integer." % val)
            return False
        if v_int < v_min:
            errors.append("Value '%s' is below minimum '%s'." % (val, v_min))
            return False
        if 0 < v_max < v_int:
            errors.append("Value '%s' is above maximum '%s'." % (val, v_max))
            return False
        return True

    def compile_connection_group(self, vhostname, groupname, val, list_out, warnings, errors):
        """
        Handle an ingressHostGroups submap.
        Each origin value is verified. On a successful run the submap
        is replaced parsed lists of HostAddr objects.
        @param[in] vhostname vhost name
        @param[in] groupname vhost/group name
        @param[in] val origin list as text string
        @param[out] list_out user inputs replaced with HostAddr objects
        @param[out] warnings nonfatal irregularities observed
        @param[out] errors descriptions of failure
        @return - origins is usable. If True then warnings[] may contain useful
                  information about fields that are ignored. If False then
                  warnings[] may contain info and errors[0] will hold the
                  description of why the origin was rejected.
        """
        key = PolicyKeys.KW_REMOTE_HOSTS
        # convert val string to list of host specs
        if isinstance(val, list):
            # ['abc', 'def', 'mytarget']
            pass
        elif isinstance(val, str):
            val = [x.strip(' ') for x in val.split(PolicyKeys.KC_CONFIG_LIST_SEP)]
        else:
            errors.append(
                "Policy vhost '%s' user group '%s' option '%s' has illegal value '%s'. Type must be 'str' or 'list' but is '%s;" %
                (vhostname, groupname, key, val, type(val)))
            return False
        for coname in val:
            try:
                coha = HostAddr(coname, PolicyKeys.KC_CONFIG_IP_SEP)
                list_out.append(coha)
            except Exception as e:
                errors.append("Policy vhost '%s' user group '%s' option '%s' connectionOption '%s' failed to translate: '%s'." %
                              (vhostname, groupname, key, coname, e))
                return False
        return True

    def compile_app_settings(self, vhostname, usergroup, policy_in, policy_out, warnings, errors):
        """
        Compile a vhostUserGroupSettings schema from processed json format to local internal format.
        @param[in] name vhost name
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
        policy_out[PolicyKeys.KW_USERS] = ''
        policy_out[PolicyKeys.KW_REMOTE_HOSTS] = ''

        # DISPATCH-2305: do not provide default values for max
        # frame/window/sessions.  The router already provides these. Setting
        # zero here will cause the router to use configured values unless
        # specifically overridden by policy:
        policy_out[PolicyKeys.KW_MAX_FRAME_SIZE] = 0
        policy_out[PolicyKeys.KW_MAX_SESSION_WINDOW] = 0
        policy_out[PolicyKeys.KW_MAX_SESSIONS] = 0

        policy_out[PolicyKeys.KW_MAX_MESSAGE_SIZE] = None
        policy_out[PolicyKeys.KW_MAX_SENDERS] = 2147483647
        policy_out[PolicyKeys.KW_MAX_RECEIVERS] = 2147483647
        policy_out[PolicyKeys.KW_ALLOW_DYNAMIC_SRC] = False
        policy_out[PolicyKeys.KW_ALLOW_ANONYMOUS_SENDER] = False
        policy_out[PolicyKeys.KW_ALLOW_USERID_PROXY] = False
        policy_out[PolicyKeys.KW_ALLOW_WAYPOINT_LINKS] = True
        policy_out[PolicyKeys.KW_ALLOW_FALLBACK_LINKS] = True
        policy_out[PolicyKeys.KW_ALLOW_DYNAMIC_LINK_ROUTES] = True
        policy_out[PolicyKeys.KW_ALLOW_ADMIN_STATUS_UPDATE] = True
        policy_out[PolicyKeys.KW_SOURCES] = ''
        policy_out[PolicyKeys.KW_TARGETS] = ''
        policy_out[PolicyKeys.KW_SOURCE_PATTERN] = ''
        policy_out[PolicyKeys.KW_TARGET_PATTERN] = ''
        policy_out[PolicyKeys.KW_MAXCONNPERHOST] = None  # optional group limit
        policy_out[PolicyKeys.KW_MAXCONNPERUSER] = None

        cerror = []
        user_sources = False
        user_targets = False
        user_src_pattern = False
        user_tgt_pattern = False
        for key, val in policy_in.items():
            if key not in self.allowed_settings_options:
                warnings.append("Policy vhost '%s' user group '%s' option '%s' is ignored." %
                                (vhostname, usergroup, key))
            if key in [PolicyKeys.KW_MAXCONNPERHOST,
                       PolicyKeys.KW_MAXCONNPERUSER
                       ]:
                if not self.validateNumber(val, 0, 65535, cerror):
                    msg = ("Policy vhost '%s' user group '%s' option '%s' has error '%s'." %
                           (vhostname, usergroup, key, cerror[0]))
                    errors.append(msg)
                    return False
                policy_out[key] = int(val)
            elif key in [PolicyKeys.KW_MAX_FRAME_SIZE,
                         PolicyKeys.KW_MAX_MESSAGE_SIZE,
                         PolicyKeys.KW_MAX_RECEIVERS,
                         PolicyKeys.KW_MAX_SENDERS,
                         PolicyKeys.KW_MAX_SESSION_WINDOW,
                         PolicyKeys.KW_MAX_SESSIONS
                         ]:
                if not self.validateNumber(val, 0, 0, cerror):
                    errors.append("Policy vhost '%s' user group '%s' option '%s' has error '%s'." %
                                  (vhostname, usergroup, key, cerror[0]))
                    return False
                policy_out[key] = int(val)
            elif key == PolicyKeys.KW_REMOTE_HOSTS:
                # Conection groups are lists of IP addresses that need to be
                # converted into binary structures for comparisons.
                val_out = []
                if not self.compile_connection_group(vhostname, usergroup, val, val_out, warnings, errors):
                    return False
                policy_out[key] = val_out
            elif key in [PolicyKeys.KW_ALLOW_ANONYMOUS_SENDER,
                         PolicyKeys.KW_ALLOW_DYNAMIC_SRC,
                         PolicyKeys.KW_ALLOW_USERID_PROXY,
                         PolicyKeys.KW_ALLOW_WAYPOINT_LINKS,
                         PolicyKeys.KW_ALLOW_FALLBACK_LINKS,
                         PolicyKeys.KW_ALLOW_DYNAMIC_LINK_ROUTES,
                         PolicyKeys.KW_ALLOW_ADMIN_STATUS_UPDATE
                         ]:
                if isinstance(val, str) and val.lower() in ['true', 'false']:
                    val = val == 'true'
                if not isinstance(val, bool):
                    errors.append("Policy vhost '%s' user group '%s' option '%s' has illegal boolean value '%s'." %
                                  (vhostname, usergroup, key, val))
                    return False
                policy_out[key] = val
            elif key in [PolicyKeys.KW_USERS,
                         PolicyKeys.KW_SOURCES,
                         PolicyKeys.KW_TARGETS,
                         PolicyKeys.KW_SOURCE_PATTERN,
                         PolicyKeys.KW_TARGET_PATTERN,
                         PolicyKeys.KW_VHOST_ALIASES
                         ]:
                # accept a string or list
                if isinstance(val, list):
                    # ['abc', 'def', 'mytarget']
                    pass
                elif isinstance(val, str):
                    # 'abc, def, mytarget'
                    val = [x.strip(' ') for x in val.split(PolicyKeys.KC_CONFIG_LIST_SEP)]
                else:
                    errors.append("Policy vhost '%s' user group '%s' option '%s' has illegal value '%s'. Type must be 'str' or 'list' but is '%s;" %
                                  (vhostname, usergroup, key, val, type(val)))
                # deduplicate address lists
                val = list(set(val))
                # val is CSV string with no white space between values: 'abc,def,mytarget,tmp-${user}'
                if key == PolicyKeys.KW_USERS:
                    # user name list items are literal strings and need no special handling
                    policy_out[key] = ','.join(val)
                else:
                    # source and target names get special handling for the '${user}' substitution token
                    # The literal string is translated to a (key, prefix, suffix) set of three strings.
                    # C code does not have to search for the username token and knows with authority
                    # how to construct match strings.
                    # A wildcard is also signaled.
                    utoken = PolicyKeys.KC_TOKEN_USER
                    eVal = []
                    for v in val:
                        vcount = v.count(utoken)
                        if vcount > 1:
                            errors.append("Policy vhost '%s' user group '%s' policy key '%s' item '%s' contains multiple user subtitution tokens" %
                                          (vhostname, usergroup, key, v))
                            return False
                        elif vcount == 1:
                            # a single token is present as a prefix, suffix, or embedded
                            # construct cChar, S1, S2 encodings to be added to eVal description
                            if v.startswith(utoken):
                                # prefix
                                eVal.append(PolicyKeys.KC_TUPLE_PREFIX)
                                eVal.append('')
                                eVal.append(v[v.find(utoken) + len(utoken):])
                            elif v.endswith(utoken):
                                # suffix
                                eVal.append(PolicyKeys.KC_TUPLE_SUFFIX)
                                eVal.append(v[0:v.find(utoken)])
                                eVal.append('')
                            else:
                                # embedded
                                if key in [PolicyKeys.KW_SOURCE_PATTERN,
                                           PolicyKeys.KW_TARGET_PATTERN]:
                                    errors.append("Policy vhost '%s' user group '%s' policy key '%s' item '%s' may contain match pattern '%s' as a prefix or a suffix only." %
                                                  (vhostname, usergroup, key, v, utoken))
                                    return False
                                eVal.append(PolicyKeys.KC_TUPLE_EMBED)
                                eVal.append(v[0:v.find(utoken)])
                                eVal.append(v[v.find(utoken) + len(utoken):])
                        else:
                            # ${user} token is absent
                            if v == PolicyKeys.KC_TUPLE_WILDCARD:
                                eVal.append(PolicyKeys.KC_TUPLE_WILDCARD)
                                eVal.append('')
                                eVal.append('')
                            else:
                                eVal.append(PolicyKeys.KC_TUPLE_ABSENT)
                                eVal.append(v)
                                eVal.append('')
                    policy_out[key] = ','.join(eVal)

                if key == PolicyKeys.KW_SOURCES:
                    user_sources = True
                if key == PolicyKeys.KW_TARGETS:
                    user_targets = True
                if key == PolicyKeys.KW_SOURCE_PATTERN:
                    user_src_pattern = True
                if key == PolicyKeys.KW_TARGET_PATTERN:
                    user_tgt_pattern = True

        if user_sources and user_src_pattern:
            errors.append("Policy vhost '%s' user group '%s' specifies conflicting 'sources' and 'sourcePattern' attributes. Use only one or the other." % (vhostname, usergroup))
            return False
        if user_targets and user_tgt_pattern:
            errors.append("Policy vhost '%s' user group '%s' specifies conflicting 'targets' and 'targetPattern' attributes. Use only one or the other." % (vhostname, usergroup))
            return False

        return True

    def compile_access_ruleset(self, name, policy_in, policy_out, warnings, errors):
        """
        Compile a vhost schema from processed json format to local internal format.
        @param[in] name vhost name
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
        policy_out[PolicyKeys.KW_MAXCONN] = 65535
        policy_out[PolicyKeys.KW_MAXCONNPERHOST] = 65535
        policy_out[PolicyKeys.KW_MAXCONNPERUSER] = 65535
        policy_out[PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT] = False
        policy_out[PolicyKeys.KW_GROUPS] = {}
        policy_out[PolicyKeys.KW_MAX_MESSAGE_SIZE] = None
        policy_out[PolicyKeys.KW_VHOST_ALIASES] = []

        # validate the options
        for key, val in policy_in.items():
            if key not in self.allowed_ruleset_options:
                warnings.append("Policy vhost '%s' option '%s' is ignored." %
                                (name, key))
            if key in [PolicyKeys.KW_MAXCONN,
                       PolicyKeys.KW_MAXCONNPERHOST,
                       PolicyKeys.KW_MAXCONNPERUSER
                       ]:
                if not self.validateNumber(val, 0, 65535, cerror):
                    msg = ("Policy vhost '%s' option '%s' has error '%s'." %
                           (name, key, cerror[0]))
                    errors.append(msg)
                    return False
                policy_out[key] = val
            elif key in [PolicyKeys.KW_MAX_MESSAGE_SIZE
                         ]:
                if not self.validateNumber(val, 0, 0, cerror):
                    msg = ("Policy vhost '%s' option '%s' has error '%s'." %
                           (name, key, cerror[0]))
                    errors.append(msg)
                    return False
                policy_out[key] = val
            elif key in [PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT]:
                if not isinstance(val, bool):
                    errors.append("Policy vhost '%s' option '%s' must be of type 'bool' but is '%s'" %
                                  (name, key, type(val)))
                    return False
                policy_out[key] = val
            elif key in [PolicyKeys.KW_VHOST_ALIASES]:
                # vhost aliases is a CSV string. convert to a list
                val0 = [x.strip(' ') for x in val.split(PolicyKeys.KC_CONFIG_LIST_SEP)]
                # Reject aliases that duplicate the vhost itself or other aliases
                val = []
                for vtest in val0:
                    if vtest == name:
                        errors.append("Policy vhost '%s' option '%s' value '%s' duplicates vhost name" %
                                      (name, key, vtest))
                        return False
                    if vtest in val:
                        errors.append("Policy vhost '%s' option '%s' value '%s' is duplicated" %
                                      (name, key, vtest))
                        return False
                    val.append(vtest)
                policy_out[key] = val
            elif key in [PolicyKeys.KW_GROUPS]:
                if not isinstance(val, dict):
                    errors.append("Policy vhost '%s' option '%s' must be of type 'dict' but is '%s'" %
                                  (name, key, type(val)))
                    return False
                for skey, sval in val.items():
                    newsettings = {}
                    if not self.compile_app_settings(name, skey, sval, newsettings, warnings, errors):
                        return False
                    policy_out[key][skey] = {}
                    policy_out[key][skey].update(newsettings)

        # Verify that each user is in only one group.
        # Create user-to-group map for looking up user's group
        policy_out[PolicyKeys.RULESET_U2G_MAP] = {}
        if PolicyKeys.KW_GROUPS in policy_out:
            for group, groupsettings in policy_out[PolicyKeys.KW_GROUPS].items():
                if PolicyKeys.KW_USERS in groupsettings:
                    users = [x.strip(' ') for x in groupsettings[PolicyKeys.KW_USERS].split(PolicyKeys.KC_CONFIG_LIST_SEP)]
                    for user in users:
                        if user in policy_out[PolicyKeys.RULESET_U2G_MAP]:
                            errors.append("Policy vhost '%s' user '%s' is in multiple user groups '%s' and '%s'" %
                                          (name, user, policy_out[PolicyKeys.RULESET_U2G_MAP][user], group))
                            return False
                        else:
                            policy_out[PolicyKeys.RULESET_U2G_MAP][user] = group
                else:
                    warnings.append("Policy vhost '%s' user group '%s' has no defined users. This policy has no effect" % (name, group))

        # Default connections require a default settings
        if policy_out[PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT]:
            if PolicyKeys.KW_DEFAULT_SETTINGS not in policy_out[PolicyKeys.KW_GROUPS]:
                errors.append("Policy vhost '%s' allows connections by default but default settings are not defined" %
                              (name))
                return False

        return True


#
#
class AppStats:
    """
    Maintain live state and statistics for an vhost.
    """

    def __init__(self, id, manager, ruleset):
        self.my_id = id
        self._manager = manager
        self.conn_mgr = PolicyAppConnectionMgr(
            ruleset[PolicyKeys.KW_MAXCONN],
            ruleset[PolicyKeys.KW_MAXCONNPERUSER],
            ruleset[PolicyKeys.KW_MAXCONNPERHOST])
        self._cstats = self._manager.get_agent().qd.qd_dispatch_policy_c_counts_alloc()
        self._manager.get_agent().add_implementation(self, "vhostStats")

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
        entitymap[PolicyKeys.KW_VHOST_NAME] =     self.my_id
        entitymap[PolicyKeys.KW_VHOST_DEPRECATED_ID]  = self.my_id
        entitymap[PolicyKeys.KW_CONNECTIONS_APPROVED] = self.conn_mgr.connections_approved
        entitymap[PolicyKeys.KW_CONNECTIONS_DENIED] =   self.conn_mgr.connections_denied
        entitymap[PolicyKeys.KW_CONNECTIONS_CURRENT] =  self.conn_mgr.connections_active
        entitymap[PolicyKeys.KW_PER_USER_STATE] =       self.conn_mgr.per_user_state
        entitymap[PolicyKeys.KW_PER_HOST_STATE] =       self.conn_mgr.per_host_state
        self._manager.get_agent().qd.qd_dispatch_policy_c_counts_refresh(self._cstats, entitymap)
        attributes.update(entitymap)

    def can_connect(self, conn_id, user, host, diags, group_max_conn_user, group_max_conn_host):
        return self.conn_mgr.can_connect(conn_id, user, host, diags, group_max_conn_user, group_max_conn_host)

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


class PolicyLocal:
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
        #  key : vhost name
        #  val : ruleset for this app
        # created by configuration
        # augmented by policy compiler
        self.rulesetdb = {}

        # settingsdb is a map
        #  key : <vhost name>
        #  val : a map
        #   key : <user group name>
        #   val : settings to use for user's connection
        # created by configuration
        self.settingsdb = {}

        # statsdb is a map
        #  key : <vhost name>
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

        # _default_vhost is a string
        #  holds the name of the vhost to use when the
        #  open.hostname is not found in the rulesetdb
        self._default_vhost = ""

        # _use_hostname_patterns
        #  holds policy setting.
        #  When true policy ruleset definitions are propagated to C code
        self.use_hostname_patterns = False

        # _max_message_size
        #  holds global value from policy config object
        self._max_message_size = 0

        # _vhost_aliases is a map
        #  key : alias vhost name
        #  val : actual vhost to which alias refers
        self._vhost_aliases = {}
    #
    # Service interfaces
    #

    def create_ruleset(self, attributes):
        """
        Create or update named policy ruleset.
        @param[in] attributes: from config
        """
        warnings = []
        diag = []
        candidate = {}
        name = attributes[PolicyKeys.KW_VHOST_NAME]
        result = self._policy_compiler.compile_access_ruleset(name, attributes, candidate, warnings, diag)

        if not result:
            raise PolicyError("Policy '%s' is invalid: %s" % (name, diag[0]))
        if len(warnings) > 0:
            for warning in warnings:
                self._manager.log_warning(warning)

        # Reject if any vhost alias name conflicts
        if name in self._vhost_aliases:
            # hostname is an alias
            raise PolicyError(
                "Policy is creating vhost '%s' but that name is already an alias for vhost '%s'" % (name, self._vhost_aliases[name]))
        for vhost_alias in candidate[PolicyKeys.KW_VHOST_ALIASES]:
            # alias is a hostname
            if vhost_alias in self.rulesetdb.keys():
                raise PolicyError(
                    "Policy for vhost '%s' defines alias '%s' which conflicts with an existing vhost named '%s'" % (name, vhost_alias, vhost_alias))
        if name not in self.rulesetdb:
            # Creating new ruleset. Vhost aliases cannot overlap
            for vhost_alias in candidate[PolicyKeys.KW_VHOST_ALIASES]:
                if vhost_alias in self._vhost_aliases:
                    raise PolicyError(
                        "Policy for vhost '%s' alias '%s' conflicts with existing alias for vhost '%s'" % (name, vhost_alias, self._vhost_aliases[vhost_alias]))
        else:
            # Updating an existing ruleset.
            # Vhost aliases still cannot overlap but replacement is allowed
            for vhost_alias in candidate[PolicyKeys.KW_VHOST_ALIASES]:
                if vhost_alias in self._vhost_aliases and not self._vhost_aliases[vhost_alias] == name:
                    raise PolicyError(
                        "Policy for vhost '%s' alias '%s' conflicts with existing alias for vhost '%s'" % (name, vhost_alias, self._vhost_aliases[vhost_alias]))

        # Reject if parse tree optimized name collision
        # Coincidently add name and aliases to parse tree
        if self.use_hostname_patterns:
            agent = self._manager.get_agent()
            # construct a list of names to be added
            tnames = []
            tnames.append(name)
            tnames += candidate[PolicyKeys.KW_VHOST_ALIASES]
            # create a list of names to undo in case a subsequent name does not work
            snames = []
            for tname in tnames:
                if not agent.qd.qd_dispatch_policy_host_pattern_add(agent.dispatch, tname):
                    # undo the snames list
                    for sname in snames:
                        agent.qd.qd_dispatch_policy_host_pattern_del(agent.dispatch, sname)
                    raise PolicyError("Policy for vhost '%s' alias '%s' optimized pattern conflicts with existing pattern" % (name, tname))
                snames.append(tname)
        # Names pass administrative approval
        if name not in self.rulesetdb:
            # add new aliases
            for nname in candidate[PolicyKeys.KW_VHOST_ALIASES]:
                self._vhost_aliases[nname] = name
            if name not in self.statsdb:
                self.statsdb[name] = AppStats(name, self._manager, candidate)
            self._manager.log_info("Created policy rules for vhost %s" % name)
        else:
            # remove old aliases
            old_aliases = self.rulesetdb[name][PolicyKeys.KW_VHOST_ALIASES]
            for oname in old_aliases:
                del self._vhost_aliases[oname]
            # add new aliases
            for nname in candidate[PolicyKeys.KW_VHOST_ALIASES]:
                self._vhost_aliases[nname] = name
            self.statsdb[name].update_ruleset(candidate)
            self._manager.log_info("Updated policy rules for vhost %s" % name)
        # TODO: ruleset lock
        self.rulesetdb[name] = {}
        self.rulesetdb[name].update(candidate)

    def policy_delete(self, name):
        """
        Delete named policy
        @param[in] name vhost name
        """
        if name not in self.rulesetdb:
            raise PolicyError("Policy '%s' does not exist" % name)
        # TODO: ruleset lock
        if self.use_hostname_patterns:
            agent = self._manager.get_agent()
            agent.qd.qd_dispatch_policy_host_pattern_remove(agent.dispatch, name)
            anames = self.rulesetdb[name][PolicyKeys.KW_VHOST_ALIASES]
            for aname in anames:
                agent.qd.qd_dispatch_policy_host_pattern_remove(agent.dispatch, aname)
        del self.rulesetdb[name]

    #
    # db enumerator
    #
    def policy_db_get_names(self):
        """
        Return a list of vhost names in this policy
        """
        return list(self.rulesetdb.keys())

    def set_default_vhost(self, name):
        """
        Set the default vhost name.
        @param name: the name of the default vhost
        @return: none
        """
        self._default_vhost = name
        self._manager.log_info("Policy fallback defaultVhost is defined: '%s'" % name)

    def default_vhost_enabled(self):
        """
        The default vhost is enabled if the name is not blank and
        the vhost is defined in rulesetdb.
        @return:
        """
        return self._default_vhost != "" and self._default_vhost in self.rulesetdb

    #
    # Runtime query interface
    #
    def lookup_vhost_alias(self, vhost_in):
        """
        Resolve given vhost name to vhost settings name.
        If the incoming name is a vhost hostname then return the same name.
        If the incoming name is a vhost alias hostname then return the containing vhost name.
        If a default vhost is defined then return its name.
        :param vhost_in: vhost name to test
        :return: name of policy settings vhost to be applied or blank if lookup failed.
        """
        vhost = vhost_in
        if self.use_hostname_patterns:
            agent = self._manager.get_agent()
            vhost = agent.qd.qd_dispatch_policy_host_pattern_lookup(agent.dispatch, vhost)
        # Translate an aliased vhost to a concrete vhost. If no alias then use current vhost.
        vhost = self._vhost_aliases.get(vhost, vhost)
        # If no usable vhost yet then try default vhost
        if vhost not in self.rulesetdb:
            vhost = self._default_vhost if self.default_vhost_enabled() else ""
        return vhost

    def lookup_user(self, user, rhost, vhost_in, conn_name, conn_id):
        """
        Lookup function called from C.
        Determine if a user on host accessing vhost through AMQP Open is allowed
        according to the policy access rules.
        If allowed then return the policy vhost settings name. If stats.can_connect
        returns true then it has registered and counted the connection.
        @param[in] user connection authId
        @param[in] rhost connection remote host numeric IP address as string
        @param[in] vhost_in vhost user is accessing
        @param[in] conn_name connection name used for tracking reports
        @param[in] conn_id internal connection id
        @return settings user-group name if allowed; "" if not allowed
        """
        try:
            # choose rule set based on incoming vhost or default vhost
            # or potential vhost found by pattern matching
            vhost = self.lookup_vhost_alias(vhost_in)
            if vhost == "":
                self._manager.log_info(
                    "DENY AMQP Open for user '%s', rhost '%s', vhost '%s': "
                    "No policy defined for vhost" % (user, rhost, vhost_in))
                return ""
            if vhost != vhost_in:
                self._manager.log_debug(
                    "AMQP Open for user '%s', rhost '%s', vhost '%s': "
                    "proceeds using vhost '%s' ruleset" % (user, rhost, vhost_in, vhost))

            ruleset = self.rulesetdb[vhost]

            # look up the stats
            if vhost not in self.statsdb:
                msg = (
                    "DENY AMQP Open for user '%s', rhost '%s', vhost '%s': "
                    "INTERNAL: Policy is defined but stats are missing" % (user, rhost, vhost))
                raise PolicyError(msg)
            stats = self.statsdb[vhost]

            # Get settings for user in a user group or in default
            if user in ruleset[PolicyKeys.RULESET_U2G_MAP]:
                usergroup = ruleset[PolicyKeys.RULESET_U2G_MAP][user]
            elif "*" in ruleset[PolicyKeys.RULESET_U2G_MAP]:
                usergroup = ruleset[PolicyKeys.RULESET_U2G_MAP]["*"]
            else:
                if ruleset[PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT]:
                    usergroup = PolicyKeys.KW_DEFAULT_SETTINGS
                else:
                    self._manager.log_info(
                        "DENY AMQP Open for user '%s', rhost '%s', vhost '%s': "
                        "User is not in a user group and unknown users are denied" % (user, rhost, vhost))
                    stats.count_other_denial()
                    return ""
            groupsettings = ruleset[PolicyKeys.KW_GROUPS][usergroup]

            # User in usergroup allowed to connect from rhost?
            allowed = False
            if PolicyKeys.KW_REMOTE_HOSTS in groupsettings:
                # Users are restricted to connecting from a rhost
                # defined by the group's remoteHost list
                cglist = groupsettings[PolicyKeys.KW_REMOTE_HOSTS]
                uhs = HostStruct(rhost)
                for cohost in cglist:
                    if cohost.match_bin(uhs):
                        allowed = True
                        break
            if not allowed:
                self._manager.log_info(
                    "DENY AMQP Open for user '%s', rhost '%s', vhost '%s': "
                    "User is not allowed to connect from this network host" % (user, rhost, vhost))
                stats.count_other_denial()
                return ""

            # This user passes administrative approval.
            # Now check live connection counts
            # Extract optional usergroup connection counts
            group_max_conn_user = groupsettings.get(PolicyKeys.KW_MAXCONNPERUSER)
            group_max_conn_host = groupsettings.get(PolicyKeys.KW_MAXCONNPERHOST)
            diags = []
            if not stats.can_connect(conn_name, user, rhost, diags, group_max_conn_user, group_max_conn_host):
                for diag in diags:
                    self._manager.log_info(
                        "DENY AMQP Open for user '%s', rhost '%s', vhost '%s': "
                        "%s" % (user, rhost, vhost, diag))
                return ""

            # Record facts about this connection to use during teardown
            facts = ConnectionFacts(user, rhost, vhost, conn_name)
            self._connections[conn_id] = facts

            # Return success
            return usergroup

        except Exception as e:
            self._manager.log_info(
                "DENY AMQP Open lookup_user failed for user '%s', rhost '%s', vhost '%s': "
                "Internal error: %s" % (user, rhost, vhost, e))
            # return failure
            return ""

    def lookup_settings(self, vhost_in, groupname, upolicy):
        """
        Given a settings name, return the aggregated policy blob.
        @param[in] vhost_in: vhost user is accessing
        @param[in] groupname: user group name
        @param[out] upolicy: dict holding policy values - the settings blob
                    TODO: make this a c struct
        @return if lookup worked
        # Note: the upolicy output is a non-nested dict with settings of interest
        """
        try:
            vhost = self.lookup_vhost_alias(vhost_in)
            if vhost != vhost_in:
                self._manager.log_debug(
                    "AMQP Open lookup settings for vhost '%s': "
                    "proceeds using vhost '%s' ruleset" % (vhost_in, vhost))

            if vhost not in self.rulesetdb:
                self._manager.log_info(
                    "lookup_settings fail for vhost '%s', user group '%s': "
                    "No policy defined for this vhost" % (vhost, groupname))
                return False

            ruleset = self.rulesetdb[vhost]

            if groupname not in ruleset[PolicyKeys.KW_GROUPS]:
                self._manager.log_trace(
                    "lookup_settings fail for vhost '%s', user group '%s': "
                    "This vhost has no settings for the user group" % (vhost, groupname))
                return False

            upolicy.update(ruleset[PolicyKeys.KW_GROUPS][groupname])

            maxsize = upolicy.get(PolicyKeys.KW_MAX_MESSAGE_SIZE, None)
            if maxsize is None:
                maxsize = ruleset.get(PolicyKeys.KW_MAX_MESSAGE_SIZE, None)
                if maxsize is None:
                    maxsize = self._max_message_size
                upolicy[PolicyKeys.KW_MAX_MESSAGE_SIZE] = maxsize

            upolicy[PolicyKeys.KW_CSTATS] = self.statsdb[vhost].get_cstats()
            return True
        except Exception as e:
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
        except Exception as e:
            self._manager.log_trace(
                "Policy internal error closing connection id %s. %s" % (conn_id, str(e)))

    def set_max_message_size(self, size):
        """
        record max message size from policy config object
        :param size:
        :return:ls

        """
        self._max_message_size = size

    #
    #
    def test_load_config(self):
        """
        Test function to load a policy.
        @return:
        """
        ruleset_str = '["vhost", {"hostname": "photoserver", "maxConnections": 50, "maxConnectionsPerUser": 5, "maxConnectionsPerHost": 20, "allowUnknownUser": true, "aliases": "antialias",'
        ruleset_str += '"groups": {'
        ruleset_str += '"anonymous":       { "users": "anonymous", "remoteHosts": "*", "maxFrameSize": 111111, "maxMessageSize": 111111, "maxSessionWindow": 111111, "maxSessions": 1, "maxSenders": 11, "maxReceivers": 11, "allowDynamicSource": false, "allowAnonymousSender": false, "sources": "public", "targets": "" },'
        ruleset_str += '"users":           { "users": "u1, u2", "remoteHosts": "*", "maxFrameSize": 222222, "maxMessageSize": 222222, "maxSessionWindow": 222222, "maxSessions": 2, "maxSenders": 22, "maxReceivers": 22, "allowDynamicSource": false, "allowAnonymousSender": false, "sources": "public, private", "targets": "public" },'
        ruleset_str += '"paidsubscribers": { "users": "p1, p2", "remoteHosts": "*", "maxFrameSize": 333333, "maxMessageSize": 333333, "maxSessionWindow": 333333, "maxSessions": 3, "maxSenders": 33, "maxReceivers": 33, "allowDynamicSource": true, "allowAnonymousSender": false, "sources": "public, private", "targets": "public, private" },'
        ruleset_str += '"test":            { "users": "zeke, ynot", "remoteHosts": "10.48.0.0-10.48.255.255, 192.168.100.0-192.168.100.255", "maxFrameSize": 444444, "maxMessageSize": 444444, "maxSessionWindow": 444444, "maxSessions": 4, "maxSenders": 44, "maxReceivers": 44, "allowDynamicSource": true, "allowAnonymousSender": true, "sources": "private", "targets": "private" },'

        if is_ipv6_enabled():
            ruleset_str += '"admin":           { "users": "alice, bob", "remoteHosts": "10.48.0.0-10.48.255.255, 192.168.100.0-192.168.100.255, 10.18.0.0-10.18.255.255, 127.0.0.1, ::1", "maxFrameSize": 555555, "maxMessageSize": 555555, "maxSessionWindow": 555555, "maxSessions": 5, "maxSenders": 55, "maxReceivers": 55, "allowDynamicSource": true, "allowAnonymousSender": true, "sources": "public, private, management", "targets": "public, private, management" },'
            ruleset_str += '"superuser":       { "users": "ellen", "remoteHosts": "72.135.2.9, 127.0.0.1, ::1", "maxFrameSize": 666666, "maxMessageSize": 666666, "maxSessionWindow": 666666, "maxSessions": 6, "maxSenders": 66, "maxReceivers": 66, "allowDynamicSource": false, "allowAnonymousSender": false, "sources": "public, private, management, root", "targets": "public, private, management, root" },'
        else:
            ruleset_str += '"admin":           { "users": "alice, bob", "remoteHosts": "10.48.0.0-10.48.255.255, 192.168.100.0-192.168.100.255, 10.18.0.0-10.18.255.255, 127.0.0.1", "maxFrameSize": 555555, "maxMessageSize": 555555, "maxSessionWindow": 555555, "maxSessions": 5, "maxSenders": 55, "maxReceivers": 55, "allowDynamicSource": true, "allowAnonymousSender": true, "sources": "public, private, management", "targets": "public, private, management" },'
            ruleset_str += '"superuser":       { "users": "ellen", "remoteHosts": "72.135.2.9, 127.0.0.1", "maxFrameSize": 666666, "maxMessageSize": 666666, "maxSessionWindow": 666666, "maxSessions": 6, "maxSenders": 66, "maxReceivers": 66, "allowDynamicSource": false, "allowAnonymousSender": false, "sources": "public, private, management, root", "targets": "public, private, management, root" },'

        ruleset_str += '"$default":        {                   "remoteHosts": "*", "maxFrameSize": 222222, "maxMessageSize": 222222, "maxSessionWindow": 222222, "maxSessions": 2, "maxSenders": 22, "maxReceivers": 22, "allowDynamicSource": false, "allowAnonymousSender": false, "sources": "public, private", "targets": "public" }'
        ruleset_str += '}}]'

        ruleset = json.loads(ruleset_str)

        self.create_ruleset(ruleset[1])
