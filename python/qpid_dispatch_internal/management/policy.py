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
import ConfigParser
import optparse
from policy_util import PolicyError, HostStruct, HostAddr, PolicyAppConnectionMgr
import pdb #; pdb.set_trace()
import ast



"""
Entity implementing the business logic of user connection/access policy.

Policy is represented several ways:

1. External       : ConfigParser-format file
2. CRUD Interface : ConfigParser file section: name, [(name, value), ...]
3. Internal       : dictionary

For example:

1. External

The External Policy is a plain ascii text file formatted for processing
by ConfigParser.

External Policy:
----------------

    [photoserver]
    schemaVersion            : 1
    policyVersion            : 1
    roles: {
      'users'           : ['u1', 'u2'],
      'paidsubscribers' : ['p1', 'p2']
      }

2. CRUD Interface

At the CRUD Create function the policy is represented by two strings:
- name : name of the ConfigParser section which is the application name
- data : ConfigParser section as a string

The CRUD Interface policy is created by ConfigParser.read(externalFile)
and then iterating through the config parser sections.

CRUD Interface Policy:
----------------------

    name: 'photoserver'
    data: '[('schemaVersion', '1'), 
            ('policyVersion', '1'), 
            ('roles', "{\n
              'users'           : ['u1', 'u2'],\n
              'paidsubscribers' : ['p1', 'p2']\n}")]'

3. Internal

Internally the policy is stored in a python dictionary. 
Policies are converted from CRUD Interface format to Internal format
by a compilation phase. The compiler sanitizes the input and
creates the nested structures needed for run-time processing.

Internal Policy:
----------------

    data['photoserver'] = 
    {'schemaVersion': 1, 
     'roles': {'paidsubscribers': ['p1', 'p2'], 
               'users': ['u1', 'u2']}, 
     'policyVersion': 1}

"""

#
#
class PolicyKeys():
    # Internal policy key words
    KW_POLICY_VERSION           = "policyVersion"
    KW_VERSION                  = "schemaVersion"
    KW_CONNECTION_ALLOW_DEFAULT = "connectionAllowDefault"
    KW_CONNECTION_ORIGINS       = "connectionOrigins"
    KW_CONNECTION_POLICY        = "connectionPolicy"
    KW_MAXCONN                  = "maximumConnections"
    KW_MAXCONNPERHOST           = "maximumConnectionsPerHost"
    KW_MAXCONNPERUSER           = "maximumConnectionsPerUser"
    KW_POLICIES                 = "policies"
    KW_ROLES                    = "roles"

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
    Compile CRUD Interface policy into Internal format.
    Validate incoming configuration for legal schema.
    - Warn about section options that go unused.
    - Disallow negative max connection numbers.
    - Check that connectionOrigins resolve to IP hosts
    """
    schema_version = 1

    schema_allowed_options = [(), (
        PolicyKeys.KW_POLICY_VERSION,
        PolicyKeys.KW_VERSION,
        PolicyKeys.KW_CONNECTION_ALLOW_DEFAULT,
        PolicyKeys.KW_CONNECTION_ORIGINS,
        PolicyKeys.KW_CONNECTION_POLICY,
        PolicyKeys.KW_MAXCONN,
        PolicyKeys.KW_MAXCONNPERHOST,
        PolicyKeys.KW_MAXCONNPERUSER,
        PolicyKeys.KW_POLICIES,
        PolicyKeys.KW_ROLES
        )
        ]

    allowed_opts = ()
    crud_compiler_fn = None


    def __init__(self, schema_version=1):
        """
        Create a validator for the given schema version.
        @param[in] schema_version version selector
        """
        if schema_version == 1:
            self.crud_compiler_fn = self.crud_compiler_v1
        else:
            raise PolicyError(
                "Illegal policy schema version %s. Must be '1'." % schema_version)
        self.schema_version  = schema_version
        self.allowed_opts    = self.schema_allowed_options[schema_version]


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


    def crud_compiler_v1_origins(self, name, submap, warnings, errors):
        """
        Handle an origins submap from a CRUD Interface request.
        Each origin value is verified. On a successful run the submap
        is replaced parsed lists of HostAddr objects.
        @param[in] name application name
        @param[in] submap CRUD Interface policy
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


    def crud_compiler_v1_policies(self, name, submap, warnings, errors):
        """
        Handle a policies submap from a CRUD Interface request.
        Validates policy only returning warnings and errors. submap is unchanged
        @param[in] name application name
        @param[in] submap CRUD Interface policy
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


    def crud_compiler_v1(self, name, policy_in, policy_out, warnings, errors):
        """
        Compile a schema from CRUD format to Internal format.
        @param[in] name application name
        @param[in] policy_in CRUD Interface policy
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
        for (key, val) in policy_in:
            if key not in self.allowed_opts:
                warnings.append("Application '%s' option '%s' is ignored." %
                                (name, key))
            if key == PolicyKeys.KW_VERSION:
                if not int(self.schema_version) == int(val):
                    errors.append("Application '%s' expected schema version '%s' but is '%s'." %
                                  (name, self.schema_version, val))
                    return False
                policy_out[key] = val
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
                         PolicyKeys.KW_ROLES
                         ]:
                try:
                    submap = ast.literal_eval(val)
                    if not type(submap) is dict:
                        errors.append("Application '%s' option '%s' must be of type 'dict' but is '%s'" %
                                      (name, key, type(submap)))
                        return False
                    if key == PolicyKeys.KW_CONNECTION_ORIGINS:
                        if not self.crud_compiler_v1_origins(name, submap, warnings, errors):
                            return False
                    elif key == PolicyKeys.KW_POLICIES:
                        if not self.crud_compiler_v1_policies(name, submap, warnings, errors):
                            return False
                    else:
                        # deduplicate connectionPolicy and roles lists
                        for k,v in submap.iteritems():
                            v = list(set(v))
                            submap[k] = v
                    policy_out[key] = submap
                except Exception, e:
                    errors.append("Application '%s' option '%s' error processing map: %s" %
                                  (name, key, e))
                    return False
        return True

class PolicyLocal():
    """
    The policy database.
    """

    def __init__(self, folder="", schema_version=1):
        """
        Create instance
        @params folder: relative path from __file__ to conf file folder
        """
        self.data = {}
        self.lookup_cache = {}
        self.stats = {}
        self.folder = folder
        self.schema_version = schema_version
        self.policy_compiler = PolicyCompiler(schema_version)
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
            if i.endswith(".conf"):
                self.policy_io_read_file(os.path.join(apath, i))

    def policy_io_read_file(self, fn):
        """
        Read a single policy config file.
        A file may hold multiple policies in separate ConfigParser sections.
        All policies validated before any are committed.
        Create each policy in db.
        @param fn: absolute path to file
        """
        try:
            cp = ConfigParser.ConfigParser()
            cp.optionxform = str
            cp.read(fn)

        except Exception, e:
            raise PolicyError( 
                "Error processing policy configuration file '%s' : %s" % (fn, e))
        newpolicies = {}
        for policy in cp.sections():
            warnings = []
            diag = []
            candidate = {}
            if not self.policy_compiler.crud_compiler_fn(policy, cp.items(policy), candidate, warnings, diag):
                msg = "Policy file '%s' is invalid: %s" % (fn, diag[0])
                raise PolicyError( msg )
            if len(warnings) > 0:
                print ("LogMe: Policy file '%s' application '%s' has warnings: %s" %
                       (fn, policy, warnings))
            newpolicies[policy] = candidate
        # Log a warning if policy from one config file replaces another.
        # TODO: Should this throw? Do we increment the policy version per load?
        for c in newpolicies:
            c_ver = 0
            e_ver = 0
            c_pol = newpolicies[c]
            if PolicyKeys.KW_POLICY_VERSION in c_pol:
                c_ver = int(c_pol[PolicyKeys.KW_POLICY_VERSION])
            if c in self.data:
                e_pol = self.data[c]
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
        self.data.update(newpolicies)


    #
    # CRUD interface
    #
    def policy_create(self, name, policy):
        """
        Create named policy
        @param name: application name
        @param policy: policy data in CRUD Interface format
        """
        warnings = []
        diag = []
        candidate = {}
        result = self.policy_compiler.crud_compiler_fn(name, policy, candidate, warnings, diag)
        if not result:
            raise PolicyError( "Policy '%s' is invalid: %s" % (name, diag[0]) )
        if len(warnings) > 0:
            print ("LogMe: Application '%s' has warnings: %s" %
                   (name, warnings))
        self.data[name] = candidate
        # TODO: Create stats

    def policy_read(self, name):
        """
        Read policy for named application
        @param[in] name application name
        @return policy data in Crud Interface format
        """
        return self.data[name]

    def policy_update(self, name, policy):
        """
        Update named policy
        @param[in] name application name
        @param[in] policy data in Crud interface format
        """
        if not name in self.data:
            raise PolicyError("Policy '%s' does not exist" % name)
        self.policy_create(name, policy)

    def policy_delete(self, name):
        """
        Delete named policy
        @param[in] name application name
        """
        if not name in self.data:
            raise PolicyError("Policy '%s' does not exist" % name)
        del self.data[name]

    #
    # db enumerator
    #
    def policy_db_get_names(self):
        """
        Return a list of application names in this policy
        """
        return self.data.keys()


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

    def policy_aggregate_policy_int(self, upolicy, policy, roles, settingname):
        """
        Pull int out of policy.policies[role] and install into upolicy.
        Integers are set to max(new, existing)
        param[in,out] upolicy user policy receiving aggregations
        param[in] policy Internal policy holding settings to be aggregated
        param[in] settingname setting of interest
        """
        if not PolicyKeys.KW_POLICIES in policy:
            return
        policies = policy[PolicyKeys.KW_POLICIES]
        for role in roles:
            if role in policies:
                rpol = policies[role]
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
                    # no setting of this name in the role's policy
                    pass
            else:
                # no policy for this role
                pass

    def policy_aggregate_policy_bool(self, upolicy, policy, roles, settingname):
        """
        Pull bool out of policy and install into upolicy if true
        param[in,out] upolicy user policy receiving aggregations
        param[in] policy Internal policy holding settings to be aggregated
        param[in] settingname setting of interest
        """
        if not PolicyKeys.KW_POLICIES in policy:
            return
        policies = policy[PolicyKeys.KW_POLICIES]
        for role in roles:
            if role in policies:
                rpol = policies[role]
                if settingname in rpol:
                    if rpol[settingname]:
                        upolicy[settingname] = True
                else:
                    # no setting of this name in the role's policy
                    pass
            else:
                # no policy for this role
                pass

    def policy_aggregate_policy_list(self, upolicy, policy, roles, settingname):
        """
        Pull list out of policy and append into upolicy
        param[in,out] upolicy user policy receiving aggregations
        param[in] policy Internal policy holding settings to be aggregated
        param[in] settingname setting of interest
        """
        if not PolicyKeys.KW_POLICIES in policy:
            return
        policies = policy[PolicyKeys.KW_POLICIES]
        for role in roles:
            if role in policies:
                rpol = policies[role]
                if settingname in rpol:
                    sp = rpol[settingname]
                    if settingname in upolicy:
                        upolicy[settingname].extend( sp )
                        upolicy[settingname] = list(set(upolicy[settingname]))
                    else:
                        # user policy doesn't have setting so force it
                        upolicy[settingname] = sp
                else:
                    # no setting of this name in the role's policy
                    pass
            else:
                # no policy for this role
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

            settings = self.data[app]
            # User allowed to connect from host?
            allowed = False
            restricted = False
            uhs = HostStruct(host)
            uroles = []
            if PolicyKeys.KW_ROLES in settings:
                for r in settings[PolicyKeys.KW_ROLES]:
                    if user in settings[PolicyKeys.KW_ROLES][r]:
                        restricted = True
                        uroles.append(r)
            uorigins = []
            if PolicyKeys.KW_CONNECTION_POLICY in settings:
                for ur in uroles:
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
            # Return connection limits and aggregation of role settings
            uroles.append(user) # user roles also includes username directly
            self.policy_aggregate_limits     (upolicy, settings, PolicyKeys.KW_POLICY_VERSION)
            self.policy_aggregate_policy_int (upolicy, settings, uroles, PolicyKeys.SETTING_MAX_FRAME_SIZE)
            self.policy_aggregate_policy_int (upolicy, settings, uroles, PolicyKeys.SETTING_MAX_MESSAGE_SIZE)
            self.policy_aggregate_policy_int (upolicy, settings, uroles, PolicyKeys.SETTING_MAX_SESSION_WINDOW)
            self.policy_aggregate_policy_int (upolicy, settings, uroles, PolicyKeys.SETTING_MAX_SESSIONS)
            self.policy_aggregate_policy_int (upolicy, settings, uroles, PolicyKeys.SETTING_MAX_SENDERS)
            self.policy_aggregate_policy_int (upolicy, settings, uroles, PolicyKeys.SETTING_MAX_RECEIVERS)
            self.policy_aggregate_policy_bool(upolicy, settings, uroles, PolicyKeys.SETTING_ALLOW_DYNAMIC_SRC)
            self.policy_aggregate_policy_bool(upolicy, settings, uroles, PolicyKeys.SETTING_ALLOW_ANONYMOUS_SENDER)
            self.policy_aggregate_policy_list(upolicy, settings, uroles, PolicyKeys.SETTING_SOURCES)
            self.policy_aggregate_policy_list(upolicy, settings, uroles, PolicyKeys.SETTING_TARGETS)
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

    print("Policy details:")
    for pname in policy.policy_db_get_names():
        print("policy : %s" % pname)
        p = ("%s" % policy.policy_read(pname))
        print(p.replace('\\n', '\n'))

    # Lookups
    upolicy = {}
    pdb.set_trace()
    res = policy.policy_lookup('192.168.100.5:33332', 'zeke', '192.168.100.5', 'photoserver', upolicy)
    print "Lookup zeke from 192.168.100.5. Expect true and maxFrameSize 44444. Result is %s" % res
    print "Resulting policy is: %s" % upolicy
    # Hit the cache
    upolicy2 = {}
    res2  = policy.policy_lookup('192.168.100.5:33335', 'zeke', '192.168.100.5', 'photoserver', upolicy2)
    # Print the stats
    print "policy stats: %s" % policy.stats

    upolicy = {}
    res = policy.policy_lookup('72.135.2.9:33333', 'ellen', '72.135.2.9', 'photoserver', upolicy)
    print "Lookup ellen from 72.135.2.9. Expect true and maxFrameSize 666666. Result is %s" % res
    print "Resulting policy is: %s" % upolicy

    upolicy = {}
    res = policy2.policy_lookup('72.135.2.9:33334', 'ellen', '72.135.2.9', 'photoserver', upolicy)
    print "Lookup policy2 ellen from 72.135.2.9. Expect false. Result is %s" % res


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
