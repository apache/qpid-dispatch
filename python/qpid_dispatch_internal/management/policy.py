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
Utilities for command-line programs.
"""

import sys, optparse, os
import ConfigParser
from collections import Sequence, Mapping
from qpid_dispatch_site import VERSION
import pdb #; pdb.set_trace()
#from traceback import format_exc
import ast



"""Entity implementing the business logic of user connection/access policy.

Reading configuration files is treated as a set of CREATE operations.

Provides interfaces for per-listener policy lookup:

- Listener accept
- AMQP Open
"""

class PolicyError(Exception):
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return repr(self.value)


class PolicyValidator():
    """
    Validate incoming configuration for legal schema.
    - Warn about section options that go unused.
    - Disallow negative max connection numbers.
    - Check that connectionOrigins resolve to IP hosts
    """
    schema_version = 1

    schema_allowed_options = [(), (
        'connectionAllowUnrestricted',
        'connectionOrigins',
        'connectionPolicy',
        'maximumConnections',
        'maximumConnectionsPerHost',
        'maximumConnectionsPerUser',
        'policies',
        'policyVersion',
        'roles',
        'schemaVersion')
        ]
    schema_disallowed_options = [(),
        ()
        ]

    allowed_opts = ()
    disallowed_opts = ()
    validator = None

    def __init__(self, schema_version=1):
        """
        Create a validator for the given schema version.
        @param[in] schema_version version selector
        """
        if schema_version != 1:
            raise PolicyError(
                "Illegal policy schema version %s. Must be '1'." % schema_version)
        self.schema_version = schema_version
        self.allowed_opts = self.schema_allowed_options[schema_version]
        self.disallowed_opts = self.schema_disallowed_options[schema_version]
        self.validator = self.validate_v1


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


    def validate_v1(self, name, policy_in, policy_out, warnings, errors):
        """
        Validate a schema.
        @param[in] name - application name
        @param[in] policy_in - section from ConfigParser as a list of tuples
        @param[out] policy_out - validated policy as nested map
        @param[out] warnings - nonfatal irregularities observed
        @param[out] errors - descriptions of failure
        @return - policy is usable
        """
        cerror = []
        # validate the options
        for (key, val) in policy_in:
            if key not in self.allowed_opts:
                warnings.append("Application '%s' option '%s' is ignored." %
                                (name, key))
            if key in self.disallowed_opts:
                errors.append("Application '%s' option '%s' is disallowed." %
                              (name, key))
                return False
            if key == "schemaVersion":
                if not int(self.schema_version) == int(val):
                    errors.append("Application '%s' expected schema version '%s' but is '%s'." %
                                  (name, self.schema_version, val))
                    return False
                policy_out[key] = val
            if key == "policyVersion":
                if not self.validateNumber(val, 0, 0, cerror):
                    errors.append("Application '%s' option '%s' must resolve to a positive integer: '%s'." %
                                    (name, key, cerror[0]))
                    return False
                policy_out[key] = val
            elif key in ['maximumConnections',
                         'maximumConnectionsPerHost',
                         'maximumConnectionsPerUser'
                         ]:
                if not self.validateNumber(val, 0, 65535, cerror):
                    msg = ("Application '%s' option '%s' has error '%s'." % 
                           (name, key, cerror[0]))
                    errors.append(msg)
                    return False
                policy_out[key] = val
            elif key in ['connectionOrigins',
                         'connectionPolicy',
                         'policies',
                         'roles'
                         ]:
                try:
                    submap = ast.literal_eval(val)
                    if not type(submap) is dict:
                        errors.append("Application '%s' option '%s' must be of type 'dict' but is '%s'" %
                                      (name, key, type(submap)))
                        return False
                    if key == "policies":
                        for pname in submap:
                            for setting in submap[pname]:
                                sval = submap[pname][setting]
                                if setting in ['max_frame_size',
                                               'max_message_size',
                                               'max_receivers',
                                               'max_senders',
                                               'max_session_window',
                                               'max_sessions'
                                               ]:
                                    if not self.validateNumber(sval, 0, 0, cerror):
                                        errors.append("Application '%s' option '%s' policy '%s' setting '%s' has error '%s'." %
                                                      (name, key, pname, setting, cerror[0]))
                                        return False
                                elif setting in ['allow_anonymous_sender',
                                                 'allow_dynamic_src'
                                                 ]:
                                    if not type(sval) is bool:
                                        errors.append("Application '%s' option '%s' policy '%s' setting '%s' has illegal boolean value '%s'." %
                                                      (name, key, pname, setting, sval))
                                        return False
                                elif setting in ['sources',
                                                 'targets'
                                                 ]:
                                    if not type(sval) is list:
                                        errors.append("Application '%s' option '%s' policy '%s' setting '%s' must be type 'list' but is '%s'." %
                                                      (name, key, pname, setting, type(sval)))
                                        return False
                                else:
                                    warnings.append("Application '%s' option '%s' policy '%s' setting '%s' is ignored." %
                                                      (name, key, pname, setting))
                    policy_out[key] = submap
                except Exception, e:
                    errors.append("Application '%s' option '%s' error processing  %s map: %s" %
                                  (name, key, e))
                    return False
        return True


class Policy():
    """The policy database."""

    data = {}
    folder = "."
    schema_version = 1
    validator = None

    def __init__(self, folder=".", schema_version=1):
        """
        Create instance
        @params folder: relative path from __file__ to conf file folder
        """
        self.folder = folder
        self.schema_version = schema_version
        self.validator = PolicyValidator(schema_version)
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
            if not self.validator.validator(policy, cp.items(policy), candidate, warnings, diag):
                msg = "Policy file '%s' is invalid: %s" % (fn, diag[0])
                raise PolicyError( msg )
            if len(warnings) > 0:
                print ("LogMe: Policy file '%s' application '%s' has warnings: %s" %
                       (fn, policy, warnings))
            newpolicies[policy] = candidate
        for newpol in newpolicies:
            self.data[newpol] = newpolicies[newpol]

    #
    # CRUD interface
    #
    def policy_create(self, name, policy, validate=True):
        """
        Create named policy
        @param name: policy name
        @param policy: policy data
        """
        warnings = []
        diag = []
        candidate = {}
        result = self.validator.validator(name, policy, candidate, warnings, diag)
        if validate and not result:
            raise PolicyError( "Policy '%s' is invalid: %s" % (name, diag[0]) )
        if len(warnings) > 0:
            print ("LogMe: Application '%s' has warnings: %s" %
                   (name, warnings))
        self.data[name] = candidate

    def policy_read(self, name):
        """Read named policy"""
        return self.data[name]

    def policy_update(self, name, policy):
        """Update named policy"""
        pass

    def policy_delete(self, name):
        """Delete named policy"""
        del self.data[name]

    #
    # db enumerator
    #
    def policy_db_get_names(self):
        """Return a list of policy names."""
        return self.data.keys()


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

    policy = Policy(options.folder)

    print("policy names: %s" % policy.policy_db_get_names())

    if options.dump:
        print("Policy details:")
        for pname in policy.policy_db_get_names():
            print("policy : %s" % pname)
            p = ("%s" % policy.policy_read(pname))
            print(p.replace('\\n', '\n'))

    newpolicy = [('versionId', 3), ('maximumConnections', '20')]
    policy.policy_create('test', newpolicy)

    print("policy names with test: %s" % policy.policy_db_get_names())

    print("policy test data:")
    print(policy.policy_read('test'))

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
