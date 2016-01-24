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

import sys, os
import json
import optparse
from policy_util import PolicyError, HostStruct, HostAddr, PolicyAppConnectionMgr
import pdb #; pdb.set_trace()



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
    KW_MAXCONN                  = "maxConnections"
    KW_MAXCONNPERHOST           = "maxConnPerHost"
    KW_MAXCONNPERUSER           = "maxConnPerUser"
    KW_USER_GROUPS              = "userGroups"
    KW_CONNECTION_GROUPS        = "connectionGroups"
    KW_CONNECTION_POLICY        = "connectionIngressPolicies"
    KW_CONNECTION_ALLOW_DEFAULT = "connectionAllowDefault"

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
        PolicyKeys.KW_CONNECTION_POLICY,
        PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT
        ]

    allowed_settings_options = [
        PolicyKeys.KW_IGNORED_NAME,
        PolicyKeys.KW_IGNORED_IDENTITY,
        PolicyKeys.KW_IGNORED_TYPE,
        PolicyKeys.KW_APPLICATION_NAME,
        PolicyKeys.KW_USER_GROUP_NAME,
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


    def compile_connection_groups(self, name, submap, warnings, errors):
        """
        Handle an connectionGroups submap.
        Each origin value is verified. On a successful run the submap
        is replaced parsed lists of HostAddr objects.
        @param[in] name application name
        @param[in,out] submap user input origin list as text strings
                       modified in place to be list of HostAddr objects
        @param[out] warnings nonfatal irregularities observed
        @param[out] errors descriptions of failure
        @return - origins is usable. If True then warnings[] may contain useful
                  information about fields that are ignored. If False then
                  warnings[] may contain info and errors[0] will hold the
                  description of why the origin was rejected.
        """
        key = PolicyKeys.KW_CONNECTION_GROUPS
        newmap = {}
        for coname in submap:
            try:
                ostr = str(submap[coname])
                olist = [x.strip(' ') for x in ostr.split(PolicyKeys.KC_CONFIG_LIST_SEP)]
                newmap[coname] = []
                for co in olist:
                    coha = HostAddr(co, PolicyKeys.KC_CONFIG_IP_SEP)
                    newmap[coname].append(coha)
            except Exception, e:
                errors.append("Application '%s' option '%s' connectionOption '%s' failed to translate: '%s'." %
                                (name, key, coname, e))
                return False
        submap.update(newmap)
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
                         PolicyKeys.KW_CONNECTION_POLICY
                         ]:
                try:
                    if not type(val) is dict:
                        errors.append("Application '%s' option '%s' must be of type 'dict' but is '%s'" %
                                      (name, key, type(val)))
                        return False
                    if key == PolicyKeys.KW_CONNECTION_GROUPS:
                        if not self.compile_connection_groups(name, val, warnings, errors):
                            return False
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
        self.policydb = {}
        self.settingsdb = {}
        self.lookup_cache = {}
        self.stats = {}
        self.policy_compiler = PolicyCompiler()
        self.name_lookup_cache = {}
        self.blob_lookup_cache = {}

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
        result = self.policy_compiler.compile_access_ruleset(name, attributes, candidate, warnings, diag)
        if not result:
            raise PolicyError( "Policy '%s' is invalid: %s" % (name, diag[0]) )
        if len(warnings) > 0:
            print ("LogMe: Application '%s' has warnings: %s" %
                   (name, warnings))
        self.policydb[name] = candidate
        # TODO: Create stats

    def create_settings(self, attributes):
        """
        Create named policy ruleset
        @param[in] attributes: from config
        """
        warnings = []
        diag = []
        candidate = {}
        app_name = attributes[PolicyKeys.KW_APPLICATION_NAME]
        usergroup = attributes[PolicyKeys.KW_USER_GROUP_NAME]
        result = self.policy_compiler.compile_app_settings(app_name, usergroup, attributes, candidate, warnings, diag)
        if not result:
            raise PolicyError( "Policy '%s' is invalid: %s" % (app_name, diag[0]) )
        if len(warnings) > 0:
            print ("LogMe: Application '%s' has warnings: %s" %
                   (app_name, warnings))
        if not app_name in self.settingsdb:
            self.settingsdb[app_name] = {}
        self.settingsdb[app_name][usergroup] = candidate  # create named settings

    def policy_read(self, name):
        """
        Read policy for named application
        @param[in] name application name
        @return policy data in raw user format
        """
        return self.policydb[name]

    def policy_update(self, name, policy):
        """
        Update named policy
        @param[in] name application name
        @param[in] policy data in raw user input
        """
        if not name in self.policydb:
            raise PolicyError("Policy '%s' does not exist" % name)
        self.policy_create(name, policy)

    def policy_delete(self, name):
        """
        Delete named policy
        @param[in] name application name
        """
        if not name in self.policydb:
            raise PolicyError("Policy '%s' does not exist" % name)
        del self.policydb[name]

    #
    # db enumerator
    #
    def policy_db_get_names(self):
        """
        Return a list of application names in this policy
        """
        return self.policydb.keys()


    #
    # Runtime query interface
    #
    def policy_aggregate_limits(self, upolicy, policy, settingname):
        """
        Force a max count value into user policy
        param[in,out] upolicy user policy receiving aggregations
        param[in] policy Internal policy holding settings to be aggregated
        param[in] settingname setting of interest
        """
        if settingname in policy:
            upolicy[settingname] = policy[settingname]

    def policy_aggregate_policy_int(self, upolicy, appsettings, groups, settingname):
        """
        Pull int out of policy.policies[group] and install into upolicy.
        Integers are set to max(new, existing)
        param[in,out] upolicy user policy receiving aggregations
        param[in] policy Internal policy holding settings to be aggregated
        param[in] settingname setting of interest
        """
        for group in groups:
            if group in appsettings:
                rpol = appsettings[group]
                if settingname in rpol:
                    sp = rpol[settingname]
                    if settingname in upolicy:
                        up = upolicy[settingname]
                        if sp > up:
                            # policy bumps up user setting
                            upolicy[settingname] = sp
                        else:
                            # user policy is already better
                            pass
                    else:
                        # user policy doesn't have setting so force it
                        upolicy[settingname] = sp
                else:
                    # no setting of this name in the group's policy
                    pass
            else:
                # no policy for this group
                pass

    def policy_aggregate_policy_bool(self, upolicy, appsettings, groups, settingname):
        """
        Pull bool out of policy and install into upolicy if true
        param[in,out] upolicy user policy receiving aggregations
        param[in] policy Internal policy holding settings to be aggregated
        param[in] settingname setting of interest
        """
        for group in groups:
            if group in appsettings:
                rpol = appsettings[group]
                if settingname in rpol:
                    if rpol[settingname]:
                        upolicy[settingname] = True
                else:
                    # no setting of this name in the group's policy
                    pass
            else:
                # no policy for this group
                pass

    def policy_aggregate_policy_list(self, upolicy, appsettings, groups, settingname):
        """
        Pull list out of policy and append into upolicy
        param[in,out] upolicy user policy receiving aggregations
        param[in] policy Internal policy holding settings to be aggregated
        param[in] settingname setting of interest
        """
        for group in groups:
            if group in appsettings:
                rpol = appsettings[group]
                if settingname in rpol:
                    sp = rpol[settingname]
                    if settingname in upolicy:
                        upolicy[settingname].extend( sp )
                        upolicy[settingname] = list(set(upolicy[settingname]))
                    else:
                        # user policy doesn't have setting so force it
                        upolicy[settingname] = sp
                else:
                    # no setting of this name in the group's policy
                    pass
            else:
                # no policy for this group
                pass

    #
    #
    def lookup_user(self, user, host, app, conn_name, policyname):
        """
        Lookup function called from C.
        Determine if a user on host accessing app through AMQP Open is allowed
        according to the policy access rules.
        If allowed then return the policy settings name
        @param[in] user connection authId
        @param[in] host connection remote host numeric IP address as string
        @param[in] app application user is accessing
        @param[out] policyname name of the policy settings blob for this user
        @return if allowed by policy
        # Note: the upolicy[0] output is list of group names joined with '|'.
        TODO: handle the AccessStats
        """
        try:
            lookup_id = user + "|" + host + "|" + app
            if lookup_id in self.name_lookup_cache:
                policyname.append( self.name_lookup_cache[lookup_id] )
                return True

            if not app in self.policydb:
                # TODO: ("LogMe: no policy defined for application %s" % app)
                policyname.append("")
                return False

            settings = self.policydb[app]
            # User allowed to connect from host?
            allowed = False
            restricted = False
            uhs = HostStruct(host)
            ugroups = []
            if PolicyKeys.KW_USER_GROUPS in settings:
                for r in settings[PolicyKeys.KW_USER_GROUPS]:
                    if user in settings[PolicyKeys.KW_USER_GROUPS][r]:
                        restricted = True
                        ugroups.append(r)
            uorigins = []
            if PolicyKeys.KW_CONNECTION_POLICY in settings:
                for ur in ugroups:
                    if ur in settings[PolicyKeys.KW_CONNECTION_POLICY]:
                        uorigins.extend(settings[PolicyKeys.KW_CONNECTION_POLICY][ur])
            if PolicyKeys.KW_CONNECTION_GROUPS in settings:
                for co in settings[PolicyKeys.KW_CONNECTION_GROUPS]:
                    if co in uorigins:
                        for cohost in settings[PolicyKeys.KW_CONNECTION_GROUPS][co]:
                            if cohost.match_bin(uhs):
                                allowed = True
                                break
                    if allowed:
                        break
            if not allowed and not restricted:
                if PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT in settings:
                    allowed = settings[PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT]
            if not allowed:
                return False
            if not restricted:
                ugroups.append(PolicyKeys.KW_DEFAULT_SETTINGS)
            #
            ugroups.sort()
            result = "|".join(ugroups)
            self.name_lookup_cache[lookup_id] = result
            policyname.append(result)
            return True

        except Exception, e:
            #print str(e)
            #pdb.set_trace()
            return False

    def lookup_settings(self, appname, name, upolicy):
        """
        Given a settings name, return the aggregated policy blob.
        @param[in] appname: application user is accessing
        @param[in] name: user group name or concatenation of names of the policy settings blob
        @param[out] upolicy: dict holding policy values - the settings blob
        @return if allowed by policy
        # Note: the upolicy output is a non-nested dict with settings of interest
        # TODO: figure out decent defaults for upolicy settings that are undefined
        """
        try:
            cachekey = appname + "|" + name
            if cachekey in self.blob_lookup_cache:
                upolicy.update( self.blob_lookup_cache[cachekey] )
                return True
            settings = self.settingsdb[appname]
            ugroups = name.split("|")
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.KW_MAX_FRAME_SIZE)
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.KW_MAX_MESSAGE_SIZE)
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.KW_MAX_SESSION_WINDOW)
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.KW_MAX_SESSIONS)
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.KW_MAX_SENDERS)
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.KW_MAX_RECEIVERS)
            self.policy_aggregate_policy_bool(upolicy, settings, ugroups, PolicyKeys.KW_ALLOW_DYNAMIC_SRC)
            self.policy_aggregate_policy_bool(upolicy, settings, ugroups, PolicyKeys.KW_ALLOW_ANONYMOUS_SENDER)
            self.policy_aggregate_policy_list(upolicy, settings, ugroups, PolicyKeys.KW_SOURCES)
            self.policy_aggregate_policy_list(upolicy, settings, ugroups, PolicyKeys.KW_TARGETS)
            c_upolicy = {}
            c_upolicy.update(upolicy)
            self.blob_lookup_cache[cachekey] = c_upolicy
            return True
        except Exception, e:
            #print str(e)
            #pdb.set_trace()
            return False

    def test_load_config(self):
        ruleset_str = '["policyAccessRuleset", {"applicationName": "photoserver","maxConnections": 50,"maxConnPerUser": 5,"maxConnPerHost": 20,"userGroups": {"anonymous":       "anonymous","users":           "u1, u2","paidsubscribers": "p1, p2","test":            "zeke, ynot","admin":           "alice, bob, ellen","superuser":       "ellen"},"connectionGroups": {"Ten18":     "10.18.0.0-10.18.255.255","EllensWS":  "72.135.2.9","TheLabs":   "10.48.0.0-10.48.255.255, 192.168.100.0-192.168.100.255","localhost": "127.0.0.1, ::1","TheWorld":  "*"},"connectionIngressPolicies": {"anonymous":       "TheWorld","users":           "TheWorld","paidsubscribers": "TheWorld","test":            "TheLabs","admin":           "Ten18, TheLabs, localhost","superuser":       "EllensWS, localhost"},"connectionAllowDefault": true}]'
        ruleset = json.loads(ruleset_str)

        self.create_ruleset(ruleset[1])

        settings_strs = []
        settings_strs.append('["policyAppSettings", {"applicationName": "photoserver","userGroupName":"anonymous",      "maxFrameSize": 111111,"maxMessageSize":   111111,"maxSessionWindow": 111111,"maxSessions":           1,"maxSenders":           11,"maxReceivers":         11,"allowDynamicSrc":      false,"allowAnonymousSender": false,"sources": "public",                           "targets": ""}]')
        settings_strs.append('["policyAppSettings", {"applicationName": "photoserver","userGroupName":"users",          "maxFrameSize": 222222,"maxMessageSize":   222222,"maxSessionWindow": 222222,"maxSessions":           2,"maxSenders":           22,"maxReceivers":         22,"allowDynamicSrc":      false,"allowAnonymousSender": false,"sources": "public, private",                  "targets": "public"}]')
        settings_strs.append('["policyAppSettings", {"applicationName": "photoserver","userGroupName":"paidsubscribers","maxFrameSize": 333333,"maxMessageSize":   333333,"maxSessionWindow": 333333,"maxSessions":           3,"maxSenders":           33,"maxReceivers":         33,"allowDynamicSrc":      true, "allowAnonymousSender": false,"sources": "public, private",                  "targets": "public, private"}]')
        settings_strs.append('["policyAppSettings", {"applicationName": "photoserver","userGroupName":"test",           "maxFrameSize": 444444,"maxMessageSize":   444444,"maxSessionWindow": 444444,"maxSessions":           4,"maxSenders":           44,"maxReceivers":         44,"allowDynamicSrc":      true, "allowAnonymousSender": true, "sources": "private",                          "targets": "private"}]')
        settings_strs.append('["policyAppSettings", {"applicationName": "photoserver","userGroupName":"admin",          "maxFrameSize": 555555,"maxMessageSize":   555555,"maxSessionWindow": 555555,"maxSessions":           5,"maxSenders":           55,"maxReceivers":         55,"allowDynamicSrc":      true, "allowAnonymousSender": true, "sources": "public, private, management",      "targets": "public, private, management"}]')
        settings_strs.append('["policyAppSettings", {"applicationName": "photoserver","userGroupName":"superuser",      "maxFrameSize": 666666,"maxMessageSize":   666666,"maxSessionWindow": 666666,"maxSessions":           6,"maxSenders":           66,"maxReceivers":         66,"allowDynamicSrc":      false,"allowAnonymousSender": false,"sources": "public, private, management, root","targets": "public, private, management, root"}]')
        settings_strs.append('["policyAppSettings", {"applicationName": "photoserver","userGroupName":"default",        "maxFrameSize": 222222,"maxMessageSize":   222222,"maxSessionWindow": 222222,"maxSessions":           2,"maxSenders":           22,"maxReceivers":         22,"allowDynamicSrc":      false,"allowAnonymousSender": false,"sources": "public, private",                  "targets": "public"}]')

        for sstr in settings_strs:
            settings = json.loads(sstr)
            self.create_settings(settings[1])


#
# HACK ALERT: Temporary
# Functions related to main
#
class ExitStatus(Exception):
    """Raised if a command wants a non-0 exit status from the script"""
    def __init__(self, status): self.status = status

def main_except(argv):

    def read_files(policy, path):
        """
        Read all .json conf files in path and create the policies they contain.
        @param policy: The policy_local to receive the configuration.
        @param path: The path relative to policy_local.py
        """
        apath = os.path.abspath(os.path.dirname(__file__))
        apath = os.path.join(apath, path)
        for i in os.listdir(apath):
            if i.endswith(".json"):
                read_file(policy, os.path.join(apath, i))

    def read_file(policy, fn):
        """
        Read a qdrouterd config file and extract the policy sections.
        @param policy: The policy_local to receive the configuration.
        @param fn: absolute path to file
        """
        try:
            with open(fn) as json_file:
                cp = json.load(json_file)
            for i in range(0, len(cp)):
                if cp[i][0] == "policyAccessRuleset":
                    policy.create_ruleset(cp[i][1])
                elif cp[i][0] == "policyAppSettings":
                    policy.create_settings(cp[i][1])
                else:
                    # some config option we don't care about
                    pass
        except Exception, e:
            # complain but otherwise ignore errors
            print("Error processing policy configuration file '%s' : %s" % (fn, e))

    usage = "usage: %prog [options]\nExercise policy_local functions."
    parser = optparse.OptionParser(usage=usage)
    parser.set_defaults(folder="")
    parser.add_option("-f", "--folder", action="store", type="string", dest="folder",
                      help="Built-in configuration settings are loaded by default or by using an empty folder string."
                      " Use '-f /some/path' to load a config from all .json files in that folder."
                      " Paths may be absolute or relative to  policy_local.py.")
    parser.add_option("-e", "--exercise", action="store_true", dest="exercise",
                      help="Run canned tests. Expect canned tests to work with configs in ../../../tests/policy-1.")

    (options, args) = parser.parse_args()

    policy = PolicyLocal()

    if options.folder == "":
        # Empty folder name uses built-in configuration
        policy.test_load_config()
    else:
        # Load all .json files in given folder
        read_files(policy, options.folder)

    print("Policy rulesets available: %s" % policy.policy_db_get_names())

    if not options.exercise:
        return

    # Exercise a few functions
    # Empty policy
    policy2 = PolicyLocal()

    print("Print some Policy details:")
    for pname in policy.policy_db_get_names():
        print("policy : %s" % pname)
        p = ("%s" % policy.policy_read(pname))
        print(p.replace('\\n', '\n'))

    # Lookups
    policynames = []
    # pdb.set_trace()
    res1 = policy.lookup_user('zeke', '192.168.100.5', 'photoserver', '192.168.100.5:33334', policynames)
    print "\nLookup zeke from 192.168.100.5. Expecting True, result is %s" % res1
    print "\nResulting policy expecting 'test', is: %s" % policynames[0]
    # Hit the cache
    policynames = []
    res2  = policy.lookup_user('zeke', '192.168.100.5', 'photoserver', '192.168.100.5:33335', policynames)

    policynames3 = []
    res3 = policy.lookup_user('ellen', '72.135.2.9', 'photoserver', '72.135.2.9:33333', policynames3)
    print "\nLookup ellen from 72.135.2.9. Expect true. Result is %s" % res3
    print "Resulting policy is: %s" % policynames[0]

    policynames = []
    res4 = policy2.lookup_user('ellen', '72.135.2.9', 'photoserver', '72.135.2.9:33334', policynames)
    print "\nLookup policy2 ellen from 72.135.2.9. Expect false. Result is %s" % res4

    upolicy6 = {}
    res6 = policy.lookup_settings('photoserver', policynames3[0], upolicy6)
    res6a = upolicy6['maxFrameSize'] == 666666
    print "\nNamed settings lookup result = %s, and value check = %s" % (res6, res6a)

    print ("Tests success: %s" % (res1 and res2 and res3 and not res4 and res6 and res6a))


def main(argv):
    try:
        main_except(argv)
        return 0
    except ExitStatus, e:
        return e.status
    except Exception, e:
        print "%s: %s"%(type(e).__name__, e)
        return 1

if __name__ == "__main__":
    sys.exit(main(sys.argv))
