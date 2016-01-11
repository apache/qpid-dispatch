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

Policy is represented several ways:

1. External       : json format file
2. Internal       : dictionary

Internal Policy:
----------------

    data['photoserver'] = 
    {'groups': {'paidsubscribers': ['p1', 'p2'],
               'users': ['u1', 'u2']}, 
     'policyVersion': 1}

"""

#
#
class PolicyKeys():
    # Policy key words
    KW_POLICY_VERSION           = "policyVersion"
    KW_CONNECTION_ALLOW_DEFAULT = "connectionAllowDefault"
    KW_CONNECTION_ORIGINS       = "connectionOrigins"
    KW_CONNECTION_POLICY        = "connectionPolicy"
    KW_MAXCONN                  = "maxConnections"
    KW_MAXCONNPERHOST           = "maxConnPerHost"
    KW_MAXCONNPERUSER           = "maxConnPerUser"
    KW_POLICIES                 = "policies"
    KW_GROUPS                   = "groups"

    SETTING_MAX_FRAME_SIZE         = "maxFrameSize"
    SETTING_MAX_MESSAGE_SIZE       = "maxMessageSize"
    SETTING_MAX_RECEIVERS          = "maxReceivers"
    SETTING_MAX_SENDERS            = "maxSenders"
    SETTING_MAX_SESSION_WINDOW     = "maxSessionWindow"
    SETTING_MAX_SESSIONS           = "maxSessions"
    SETTING_ALLOW_ANONYMOUS_SENDER = "allowAnonymousSender"
    SETTING_ALLOW_DYNAMIC_SRC      = "allowDynamicSrc"
    SETTING_SOURCES                = "sources"
    SETTING_TARGETS                = "targets"
#
#
class PolicyCompiler():
    """
    Validate incoming configuration for legal schema.
    - Warn about section options that go unused.
    - Disallow negative max connection numbers.
    - Check that connectionOrigins resolve to IP hosts
    """

    allowed_options = [
        PolicyKeys.KW_POLICY_VERSION,
        PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT,
        PolicyKeys.KW_CONNECTION_ORIGINS,
        PolicyKeys.KW_CONNECTION_POLICY,
        PolicyKeys.KW_MAXCONN,
        PolicyKeys.KW_MAXCONNPERHOST,
        PolicyKeys.KW_MAXCONNPERUSER,
        PolicyKeys.KW_POLICIES,
        PolicyKeys.KW_GROUPS
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


    def compiler_origins(self, name, submap, warnings, errors):
        """
        Handle an origins submap.
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
        key = PolicyKeys.KW_CONNECTION_ORIGINS
        newmap = {}
        for coname in submap:
            try:
                olist = submap[coname]
                if not type(olist) is list:
                    errors.append("Application '%s' option '%s' connectionOption '%s' must be type 'list' but is '%s'." %
                                    (name, key, coname, type(olist)))
                    return False
                newmap[coname] = []
                for co in olist:
                    coha = HostAddr(co)
                    newmap[coname].append(coha)
            except Exception, e:
                errors.append("Application '%s' option '%s' connectionOption '%s' failed to translate: '%s'." %
                                (name, key, coname, e))
                return False
        submap.update(newmap)
        return True


    def compiler_policies(self, name, submap, warnings, errors):
        """
        Handle a policies submap
        Validates policy only returning warnings and errors. submap is unchanged
        @param[in] name application name
        @param[in] submap user input policy submap
        @param[out] warnings nonfatal irregularities observed
        @param[out] errors descriptions of failure
        @return - policy is usable. If True then warnings[] may contain useful
                  information about fields that are ignored. If False then
                  warnings[] may contain info and errors[0] will hold the
                  description of why the policy was rejected.
        """
        key = PolicyKeys.KW_POLICIES
        cerror = []
        for pname in submap:
            for setting in submap[pname]:
                sval = submap[pname][setting]
                if setting in [PolicyKeys.SETTING_MAX_FRAME_SIZE,
                               PolicyKeys.SETTING_MAX_MESSAGE_SIZE,
                               PolicyKeys.SETTING_MAX_RECEIVERS,
                               PolicyKeys.SETTING_MAX_SENDERS,
                               PolicyKeys.SETTING_MAX_SESSION_WINDOW,
                               PolicyKeys.SETTING_MAX_SESSIONS
                               ]:
                    if not self.validateNumber(sval, 0, 0, cerror):
                        errors.append("Application '%s' option '%s' policy '%s' setting '%s' has error '%s'." %
                                      (name, key, pname, setting, cerror[0]))
                        return False
                elif setting in [PolicyKeys.SETTING_ALLOW_ANONYMOUS_SENDER,
                                 PolicyKeys.SETTING_ALLOW_DYNAMIC_SRC
                                 ]:
                    if not type(sval) is bool:
                        errors.append("Application '%s' option '%s' policy '%s' setting '%s' has illegal boolean value '%s'." %
                                      (name, key, pname, setting, sval))
                        return False
                elif setting in [PolicyKeys.SETTING_SOURCES,
                                 PolicyKeys.SETTING_TARGETS
                                 ]:
                    if not type(sval) is list:
                        errors.append("Application '%s' option '%s' policy '%s' setting '%s' must be type 'list' but is '%s'." %
                                      (name, key, pname, setting, type(sval)))
                        return False
                    # deduplicate address lists
                    sval = list(set(sval))
                    submap[pname][setting] = sval
                else:
                    warnings.append("Application '%s' option '%s' policy '%s' setting '%s' is ignored." %
                                     (name, key, pname, setting))
        return True


    def compile(self, name, policy_in, policy_out, warnings, errors):
        """
        Compile a schema from processed json format to Internal format.
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
            if key not in self.allowed_options:
                warnings.append("Application '%s' option '%s' is ignored." %
                                (name, key))
            if key == PolicyKeys.KW_POLICY_VERSION:
                if not self.validateNumber(val, 0, 0, cerror):
                    errors.append("Application '%s' option '%s' must resolve to a positive integer: '%s'." %
                                    (name, key, cerror[0]))
                    return False
                policy_out[key] = val
            elif key in [PolicyKeys.KW_MAXCONN,
                         PolicyKeys.KW_MAXCONNPERHOST,
                         PolicyKeys.KW_MAXCONNPERUSER
                         ]:
                if not self.validateNumber(val, 0, 65535, cerror):
                    msg = ("Application '%s' option '%s' has error '%s'." % 
                           (name, key, cerror[0]))
                    errors.append(msg)
                    return False
                policy_out[key] = val
            elif key in [PolicyKeys.KW_CONNECTION_ORIGINS,
                         PolicyKeys.KW_CONNECTION_POLICY,
                         PolicyKeys.KW_POLICIES,
                         PolicyKeys.KW_GROUPS
                         ]:
                try:
                    if not type(val) is dict:
                        errors.append("Application '%s' option '%s' must be of type 'dict' but is '%s'" %
                                      (name, key, type(val)))
                        return False
                    if key == PolicyKeys.KW_CONNECTION_ORIGINS:
                        if not self.compiler_origins(name, val, warnings, errors):
                            return False
                    elif key == PolicyKeys.KW_POLICIES:
                        if not self.compiler_policies(name, val, warnings, errors):
                            return False
                    else:
                        # deduplicate connectionPolicy and groups lists
                        for k,v in val.iteritems():
                            v = list(set(v))
                            val[k] = v
                    policy_out[key] = val
                except Exception, e:
                    errors.append("Application '%s' option '%s' error processing map: %s" %
                                  (name, key, e))
                    return False
        return True

class PolicyLocal():
    """
    The policy database.
    """

    def __init__(self, folder=""):
        """
        Create instance
        @params folder: relative path from __file__ to conf file folder
        """
        self.policydb = {}
        self.lookup_cache = {}
        self.stats = {}
        self.folder = folder
        self.policy_compiler = PolicyCompiler()
        if not folder == "":
            self.policy_io_read_files()

    #
    # Policy file I/O
    #
    def policy_io_read_files(self):
        """
        Read all conf files and create the policies they contain.
        """
        apath = os.path.abspath(os.path.dirname(__file__))
        apath = os.path.join(apath, self.folder)
        for i in os.listdir(apath):
            if i.endswith(".json"):
                self.policy_io_read_file(os.path.join(apath, i))

    def policy_io_read_file(self, fn):
        """
        Read a policy config file.
        Validate each policy and commit to policy database.
        @param fn: absolute path to file
        """
        try:
            with open(fn) as json_file:
                cp = json.load(json_file)

        except Exception, e:
            raise PolicyError( 
                "Error processing policy configuration file '%s' : %s" % (fn, e))
        newpolicies = {}
        for app_name, app_policy in cp.iteritems():
            warnings = []
            diag = []
            candidate = {}
            if not self.policy_compiler.compile(app_name, app_policy, candidate, warnings, diag):
                msg = "Policy file '%s' is invalid: %s" % (fn, diag[0])
                raise PolicyError( msg )
            if len(warnings) > 0:
                print ("LogMe: Policy file '%s' application '%s' has warnings: %s" %
                       (fn, app_name, warnings))
            newpolicies[app_name] = candidate
        # Log a warning if policy from one config file replaces another.
        # TODO: Should this throw? Do we increment the policy version per load?
        for c in newpolicies:
            c_ver = 0
            e_ver = 0
            c_pol = newpolicies[c]
            if PolicyKeys.KW_POLICY_VERSION in c_pol:
                c_ver = int(c_pol[PolicyKeys.KW_POLICY_VERSION])
            if c in self.policydb:
                e_pol = self.policydb[c]
                if PolicyKeys.KW_POLICY_VERSION in e_pol:
                    e_ver = int(e_pol[PolicyKeys.KW_POLICY_VERSION])
                if c_ver < e_ver:
                    kw = "downgrades"
                elif c_ver == e_ver:
                    kw = "replaces"
                else:
                    kw = "upgrades"
                msg = ("LogMe: WARNING Policy file '%s' application '%s' policy version '%s' %s existing policy version '%s'." %
                    (fn, c, c_ver, kw, e_ver))
                print msg
        for c in newpolicies:
            c_pol = newpolicies[c]
            c_max = 0
            c_max_u = 0
            c_max_h = 0
            if PolicyKeys.KW_MAXCONN in c_pol:
                c_max = c_pol[PolicyKeys.KW_MAXCONN]
            if PolicyKeys.KW_MAXCONNPERUSER in c_pol:
                c_max_u = c_pol[PolicyKeys.KW_MAXCONNPERUSER]
            if PolicyKeys.KW_MAXCONNPERHOST in c_pol:
                c_max_h = c_pol[PolicyKeys.KW_MAXCONNPERHOST]
            if c in self.stats:
                self.stats[c].update(c_max, c_max_u, c_max_h)
            else:
                self.stats[c] = PolicyAppConnectionMgr(c_max, c_max_u, c_max_h)
        self.policydb.update(newpolicies)


    #
    # Service interfaces
    #
    def policy_create(self, name, policy):
        """
        Create named policy
        @param name: application name
        @param policy: policy data in raw user format
        """
        warnings = []
        diag = []
        candidate = {}
        result = self.policy_compiler.compile(name, policy, candidate, warnings, diag)
        if not result:
            raise PolicyError( "Policy '%s' is invalid: %s" % (name, diag[0]) )
        if len(warnings) > 0:
            print ("LogMe: Application '%s' has warnings: %s" %
                   (name, warnings))
        self.policydb[name] = candidate
        # TODO: Create stats

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

    def policy_aggregate_policy_int(self, upolicy, policy, groups, settingname):
        """
        Pull int out of policy.policies[group] and install into upolicy.
        Integers are set to max(new, existing)
        param[in,out] upolicy user policy receiving aggregations
        param[in] policy Internal policy holding settings to be aggregated
        param[in] settingname setting of interest
        """
        if not PolicyKeys.KW_POLICIES in policy:
            return
        policies = policy[PolicyKeys.KW_POLICIES]
        for group in groups:
            if group in policies:
                rpol = policies[group]
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

    def policy_aggregate_policy_bool(self, upolicy, policy, groups, settingname):
        """
        Pull bool out of policy and install into upolicy if true
        param[in,out] upolicy user policy receiving aggregations
        param[in] policy Internal policy holding settings to be aggregated
        param[in] settingname setting of interest
        """
        if not PolicyKeys.KW_POLICIES in policy:
            return
        policies = policy[PolicyKeys.KW_POLICIES]
        for group in groups:
            if group in policies:
                rpol = policies[group]
                if settingname in rpol:
                    if rpol[settingname]:
                        upolicy[settingname] = True
                else:
                    # no setting of this name in the group's policy
                    pass
            else:
                # no policy for this group
                pass

    def policy_aggregate_policy_list(self, upolicy, policy, groups, settingname):
        """
        Pull list out of policy and append into upolicy
        param[in,out] upolicy user policy receiving aggregations
        param[in] policy Internal policy holding settings to be aggregated
        param[in] settingname setting of interest
        """
        if not PolicyKeys.KW_POLICIES in policy:
            return
        policies = policy[PolicyKeys.KW_POLICIES]
        for group in groups:
            if group in policies:
                rpol = policies[group]
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

    def policy_lookup_settings(self, user, host, app, upolicy):
        """
        Determine if a user on host accessing app through AMQP Open is allowed
        according to the policy access rules. 
        If allowed then return the policy settings.
        @param[in] user connection authId
        @param[in] host connection remote host numeric IP address
        @param[in] app application user is accessing
        @param[out] upolicy dict holding connection and policy values
        @return if allowed by policy
        # Note: the upolicy output is a non-nested dict with settings of interest
        # TODO: figure out decent defaults for upolicy settings that are undefined
        """
        try:
            lookup_id = user + "|" + host + "|" + app
            if lookup_id in self.lookup_cache:
                upolicy.update( self.lookup_cache[lookup_id] )
                return True

            settings = self.policydb[app]
            # User allowed to connect from host?
            allowed = False
            restricted = False
            uhs = HostStruct(host)
            ugroups = []
            if PolicyKeys.KW_GROUPS in settings:
                for r in settings[PolicyKeys.KW_GROUPS]:
                    if user in settings[PolicyKeys.KW_GROUPS][r]:
                        restricted = True
                        ugroups.append(r)
            uorigins = []
            if PolicyKeys.KW_CONNECTION_POLICY in settings:
                for ur in ugroups:
                    if ur in settings[PolicyKeys.KW_CONNECTION_POLICY]:
                        uorigins.extend(settings[PolicyKeys.KW_CONNECTION_POLICY][ur])
            if PolicyKeys.KW_CONNECTION_ORIGINS in settings:
                for co in settings[PolicyKeys.KW_CONNECTION_ORIGINS]:
                    if co in uorigins:
                        for cohost in settings[PolicyKeys.KW_CONNECTION_ORIGINS][co]:
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
            # Return connection limits and aggregation of group settings
            ugroups.append(user) # user groups also includes username directly
            self.policy_aggregate_limits     (upolicy, settings, PolicyKeys.KW_POLICY_VERSION)
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.SETTING_MAX_FRAME_SIZE)
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.SETTING_MAX_MESSAGE_SIZE)
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.SETTING_MAX_SESSION_WINDOW)
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.SETTING_MAX_SESSIONS)
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.SETTING_MAX_SENDERS)
            self.policy_aggregate_policy_int (upolicy, settings, ugroups, PolicyKeys.SETTING_MAX_RECEIVERS)
            self.policy_aggregate_policy_bool(upolicy, settings, ugroups, PolicyKeys.SETTING_ALLOW_DYNAMIC_SRC)
            self.policy_aggregate_policy_bool(upolicy, settings, ugroups, PolicyKeys.SETTING_ALLOW_ANONYMOUS_SENDER)
            self.policy_aggregate_policy_list(upolicy, settings, ugroups, PolicyKeys.SETTING_SOURCES)
            self.policy_aggregate_policy_list(upolicy, settings, ugroups, PolicyKeys.SETTING_TARGETS)
            c_upolicy = {}
            c_upolicy.update(upolicy)
            self.lookup_cache[lookup_id] = c_upolicy
            return True
        except Exception, e:
            #print str(e)
            #pdb.set_trace()
            return False

    def policy_lookup(self, conn_id, user, host, app, upolicy):
        """
        Determine if a user on host accessing app through AMQP Open is allowed:
        - verify to the policy access rules. 
        - track user/host connection limits
        If allowed then return the policy settings.
        @param[in] conn_id unique connection identifier
        @param[in] user connection authId
        @param[in] host connection remote host numeric IP address
        @param[in] app application user is accessing
        @param[out] upolicy dict holding connection and policy values
        @return if allowed by policy
        # Note: the upolicy output is a non-nested dict with settings of interest
        # TODO: figure out decent defaults for upolicy settings that are undefined
        """
        if not self.policy_lookup_settings(user, host, app, upolicy):
            # TODO: print ("LogMe: connection denied by connection access rules")
            return False
        assert(app in self.stats)
        diags = []
        if not self.stats[app].can_connect(conn_id, user, host, diags):
            # TODO: print ("LogMe: connection denied by connection count limits: %s" % diags)
            return False
        return True


#
# HACK ALERT: Temporary
# Functions related to main
#
class ExitStatus(Exception):
    """Raised if a command wants a non-0 exit status from the script"""
    def __init__(self, status): self.status = status

def main_except(argv):

    usage = "usage: %prog [options]\nRead and print all conf files in a folder."
    parser = optparse.OptionParser(usage=usage)
    parser.set_defaults(folder="../../../tests/policy-1")
    parser.add_option("-f", "--folder", action="store", type="string", dest="folder",
                      help="Use named folder instead of policy-1")
    parser.add_option("-d", "--dump", action="store_true", dest="dump",
                      help="Dump policy details")

    (options, args) = parser.parse_args()

    policy = PolicyLocal(options.folder)

    print("policy names: %s" % policy.policy_db_get_names())

    if not options.dump:
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
    upolicy = {}
    # pdb.set_trace()
    res1 = policy.policy_lookup('192.168.100.5:33332', 'zeke', '192.168.100.5', 'photoserver', upolicy)
    print "\nLookup zeke from 192.168.100.5. Expect true and maxFrameSize 44444. Expecting True, result is %s" % res1
    print "\nResulting policy is: %s" % upolicy
    # Hit the cache
    upolicy2 = {}
    res2  = policy.policy_lookup('192.168.100.5:33335', 'zeke', '192.168.100.5', 'photoserver', upolicy2)
    # Print the stats
    print "\npolicy stats: %s" % policy.stats

    upolicy = {}
    res3 = policy.policy_lookup('72.135.2.9:33333', 'ellen', '72.135.2.9', 'photoserver', upolicy)
    print "\nLookup ellen from 72.135.2.9. Expect true and maxFrameSize 666666. Result is %s" % res3
    print "Resulting policy is: %s" % upolicy

    upolicy = {}
    res4 = policy2.policy_lookup('72.135.2.9:33334', 'ellen', '72.135.2.9', 'photoserver', upolicy)
    print "\nLookup policy2 ellen from 72.135.2.9. Expect false. Result is %s" % res4

    if not (res1 and res2 and res3 and not res4):
        print "Tests FAIL"
    else:
        print "Tests PASS"

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
