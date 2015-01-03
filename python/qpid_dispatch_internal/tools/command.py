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

import os, sys, json, optparse
from collections import Sequence, Mapping
from qpid_dispatch_site import VERSION
from textwrap import fill

class UsageError(Exception):
    """
    Raise this exception to indicate the usage message should be printed.
    Handled by L{main}
    """
    pass

def main(run, argv=sys.argv, op=None):
    """
    Call run(argv) with exception handling, do appropriate sys.exit().
    @param op: an OptionParser to use for usage related error messages.
    @return: exit value for sys.exit
    """
    try:
        run(argv)
        return 0
    except KeyboardInterrupt:
        print
    except UsageError, e:
        op.error(e)
    except Exception, e:
        print "%s: %s" % (type(e).__name__, e)
    return 1

def check_args(args, maxargs=0, minargs=0):
    """
    Check number of arguments, raise UsageError if in correct.
    @param maxargs: max number of allowed args after command or None to skip check.
    @param minargs: min number of allowed args after command or None to skip check.
    """
    if minargs is not None and len(args) < minargs:
        raise UsageError("Not enough arguments, got %s need %s" % (len(args), minargs))
    if maxargs is not None and len(args) > maxargs:
        raise UsageError("Unexpected arguments: %s" % (" ".join(args[maxargs:])))

def connection_options(options, title="Connection Options"):
    """Return an OptionGroup for connection options."""
    group = optparse.OptionGroup(options, title)
    group.add_option("-b", "--bus", action="store", type="string", default="0.0.0.0",
                     metavar="<url>", help="URL of the messaging bus to connect to (default %default)")
    group.add_option("-r", "--router", action="store", type="string", default=None,
                     metavar="<router-id>", help="Router to be queried")
    group.add_option("-t", "--timeout", action="store", type="float", default=5, metavar="<secs>",
                      help="Maximum time to wait for connection in seconds (default %default)")
    group.add_option("--sasl-mechanism", action="store", type="string", metavar="<mech>",
                      help="Force SASL mechanism (e.g. EXTERNAL, ANONYMOUS, PLAIN, CRAM-MD5, DIGEST-MD5, GSSAPI).")
    group.add_option("--ssl-certificate", action="store", type="string", metavar="<cert>",
                     help="Client SSL certificate (PEM Format)")
    group.add_option("--ssl-key", action="store", type="string", metavar="<key>",
                     help="Client SSL private key (PEM Format)")
    return group


class Option(optparse.Option):
    """Addes two new types to optparse.Option: json_map, json_list"""

    def check_json(option, opt, value):
        """Validate a json value, for use with L{Option}"""
        try:
            result = json.loads(value)
            if option.type == 'json_list' and not isinstance(result, Sequence) or \
               option.type == 'json_map' and not isinstance(result, Mapping):
                raise ValueError()
            return result
        except ValueError:
            raise optparse.OptionValueError("%s: invalid %s: %r" % (opt, option.type, value))

    TYPES = optparse.Option.TYPES + ("json_list", "json_map")
    TYPE_CHECKER = dict(optparse.Option.TYPE_CHECKER, json_list=check_json, json_map=check_json)


class OptionParser(optparse.OptionParser):
    """Adds standard --version option to optparse.OptionParser"""
    def __init__(self, *args, **kwargs):
        optparse.OptionParser.__init__(self, *args, **kwargs)
        def version_cb(*args):
            print VERSION
            exit(0)

        self.add_option("--version", help="Print version and exit.",
                        action="callback", callback=version_cb)

