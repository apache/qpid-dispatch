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
import sys
import argparse
import os

from qpid_dispatch_site import VERSION
from proton import SSLDomain, Url


class UsageError(Exception):
    """
    Raise this exception to indicate the usage message should be printed.
    Handled by L{main}
    """
    pass


def main(run, argv=None, parser=None):
    """
    Call run(argv) with exception handling, do appropriate sys.exit().
    @param parser: a Parser to use for usage related error messages.
    @return: exit value for sys.exit
    """
    try:
        run(argv or sys.argv)
        return 0
    except KeyboardInterrupt:
        print()
    except UsageError as e:
        parser.error(e)
    except Exception as e:
        if "_QPID_DISPATCH_TOOLS_DEBUG_" in os.environ:
            raise
        print("%s: %s" % (type(e).__name__, e))
    return 1


def check_args(args, maxargs=0, minargs=0):
    """
    Check number of arguments, raise UsageError if in correct.
    @param maxargs: max number of allowed args after command or None to skip check.
    @param minargs: min number of allowed args after command or None to skip check.
    @return args padded with None to maxargs.
    """
    if minargs is not None and len(args) < minargs:
        raise UsageError("Not enough arguments, got %s need %s" % (len(args), minargs))
    if maxargs is not None and len(args) > maxargs:
        raise UsageError("Unexpected arguments: %s" % (" ".join(args[maxargs:])))
    return args + [None] * (maxargs - len(args))


def parse_args_qdstat(BusManager, argv=None):
    parser = _qdstat_parser(BusManager)
    return parser.parse_args(args=argv)


def parse_args_qdmanage(operations, argv=None):
    parser = _qdmanage_parser(operations)
    return parser.parse_known_args(args=argv)


common_parser = argparse.ArgumentParser(add_help=False)
common_parser.add_argument('--version', action='version', version=VERSION)
common_parser.add_argument("-v", "--verbose", help="Show maximum detail",
                           action="count")  # support -vvv


def _custom_optional_arguments_parser(*args, **kwargs):
    parser = argparse.ArgumentParser(*args, **kwargs)
    parser._optionals.title = "Optional Arguments"
    return parser


def add_connection_options(parser):
    group = parser.add_argument_group('Connection Options')
    group.add_argument("-b", "--bus", default="0.0.0.0",
                       metavar="URL", help="URL of the messaging bus to connect to default %(default)s")
    group.add_argument("-t", "--timeout", type=float, default=5, metavar="SECS",
                       help="Maximum time to wait for connection in seconds default %(default)s")
    group.add_argument("--ssl-certificate", metavar="CERT",
                       help="Client SSL certificate (PEM Format)")
    group.add_argument("--ssl-key", metavar="KEY",
                       help="Client SSL private key (PEM Format)")
    group.add_argument("--ssl-trustfile", metavar="TRUSTED-CA-DB",
                       help="Trusted Certificate Authority Database file (PEM Format)")
    group.add_argument("--ssl-password", metavar="PASSWORD",
                       help="Certificate password, will be prompted if not specifed.")
    # Use the --ssl-password-file option to avoid having the --ssl-password in history or scripts.
    group.add_argument("--ssl-password-file", metavar="SSL-PASSWORD-FILE",
                       help="Certificate password, will be prompted if not specifed.")

    group.add_argument("--sasl-mechanisms", metavar="SASL-MECHANISMS",
                       help="Allowed sasl mechanisms to be supplied during the sasl handshake.")
    group.add_argument("--sasl-username", metavar="SASL-USERNAME",
                       help="User name for SASL plain authentication")
    group.add_argument("--sasl-password", metavar="SASL-PASSWORD",
                       help="Password for SASL plain authentication")
    # Use the --sasl-password-file option to avoid having the --sasl-password in history or scripts.
    group.add_argument("--sasl-password-file", metavar="SASL-PASSWORD-FILE",
                       help="Password for SASL plain authentication")
    group.add_argument("--ssl-disable-peer-name-verify", action="store_true",
                       help="Disables SSL peer name verification. WARNING - This option is insecure and must not be used "
                       "in production environments")


def _qdstat_add_display_args(parser, BusManager):
    _group = parser.add_argument_group('Display', 'Choose what kind of \
                                                   information you want to be displayed')
    display = _group.add_mutually_exclusive_group(required=False)
    display.add_argument("-g", "--general", action="store_const", dest="show",
                         help="Show General Router Stats",
                         const=BusManager.displayGeneral.__name__)
    display.add_argument("-c", "--connections", action="store_const", dest="show",
                         help="Show Connections",
                         const=BusManager.displayConnections.__name__)
    display.add_argument("-l", "--links", action="store_const", dest="show",
                         help="Show Router Links",
                         const=BusManager.displayRouterLinks.__name__)
    display.add_argument("-n", "--nodes", action="store_const", dest="show",
                         help="Show Router Nodes",
                         const=BusManager.displayRouterNodes.__name__)
    display.add_argument("-e", "--edge", action="store_const", dest="show",
                         help="Show edge connections",
                         const=BusManager.displayEdges.__name__)
    display.add_argument("-a", "--address", action="store_const", dest="show",
                         help="Show Router Addresses",
                         const=BusManager.displayAddresses.__name__)
    display.add_argument("-m", "--memory", action="store_const", dest="show",
                         help="Show Router Memory Stats",
                         const=BusManager.displayMemory.__name__)
    display.add_argument("-p", "--policy", action="store_const", dest="show",
                         help="Show Router Policy",
                         const=BusManager.displayPolicy.__name__)
    display.add_argument("--autolinks", action="store_const", dest="show",
                         help="Show Auto Links",
                         const=BusManager.displayAutolinks.__name__)
    display.add_argument("--linkroutes", action="store_const", dest="show",
                         help="Show Link Routes",
                         const=BusManager.displayLinkRoutes.__name__)
    display.add_argument("--vhosts", action="store_const", dest="show",
                         help="Show Vhosts",
                         const=BusManager.displayVhosts.__name__)
    display.add_argument("--vhostgroups", action="store_const", dest="show",
                         help="Show Vhost Groups",
                         const=BusManager.displayVhostgroups.__name__)
    display.add_argument("--vhoststats", action="store_const", dest="show",
                         help="Show Vhost Stats",
                         const=BusManager.displayVhoststats.__name__)
    display.add_argument("--log", action="store_const", dest="show",
                         help="Show recent log entries",
                         const=BusManager.displayLog.__name__)
    display.add_argument("--all-entities", action="store_const", dest="show",
                         help="Show all router entities. Can be combined with --all-routers option",
                         const=BusManager.show_all.__name__)

    display.set_defaults(show=BusManager.displayGeneral.__name__)


def _qdstat_parser(BusManager):
    parser = _custom_optional_arguments_parser(prog="qdstat", parents=[common_parser])
    _qdstat_add_display_args(parser, BusManager)

    _group = parser.add_argument_group('Target', 'Choose destination router to \
                                                  required, default the one you connect to.')
    target = _group.add_mutually_exclusive_group(required=False)
    target.add_argument("--all-routers", action="store_true",
                        help="Show entities for all routers in network. \
                        Can also be used in combination with other options")
    target.add_argument("-r", "--router",
                        metavar="ROUTER-ID", help="Router to be queried")
    target.add_argument("-d", "--edge-router", metavar="EDGE-ROUTER-ID", help="Edge Router to be queried")

    # This limit can be used to limit the number of output rows and
    # can be used in conjunction with options
    # like -c, -l, -a, --autolinks, --linkroutes and --log.
    # By default, the limit is not set, which means the limit is unlimited.
    parser.add_argument("--limit", help="Limit number of output rows. Unlimited if limit is zero or if limit not specified", type=int, default=None)
    parser.add_argument("--csv", help="Render tabular output in csv format", action="store_true")

    add_connection_options(parser)
    return parser


def _qdmanage_add_args(parser):
    parser.add_argument("-r", "--router",
                        metavar="ROUTER-ID", help="Router to be queried")
    # Edge routers are not part of the router network. Hence we need a separate option
    # to be able to query edge routers
    parser.add_argument("-d", '--edge-router', metavar="EDGE-ROUTER-ID", help='Edge Router to be queried')
    parser.add_argument('--type', help='Type of entity to operate on.')  # add choices
    parser.add_argument('--name', help='Name of entity to operate on.')
    parser.add_argument('--identity', help='Identity of entity to operate on.',
                        metavar="ID")
    parser.add_argument("--indent", type=int, default=2,
                        help="Pretty-printing indent. -1 means don't pretty-print (default %(default)s)")
    parser.add_argument('--stdin', action='store_true',
                        help='Read attributes as JSON map or list of maps from stdin.')
    parser.add_argument('--body', help='JSON value to use as body of a non-standard operation call.')
    parser.add_argument('--properties', help='JSON map to use as properties for a non-standard operation call.')


def _qdmanage_parser(operations):
    description = "Standard operations: %s. Use GET-OPERATIONS to find additional operations." % (", ".join(operations))
    parser = _custom_optional_arguments_parser(prog="qdmanage <operation>",
                                               parents=[common_parser],
                                               description=description)
    _qdmanage_add_args(parser)
    add_connection_options(parser)
    return parser


def get_password(file=None):
    if file:
        with open(file, 'r') as password_file:
            return str(password_file.read()).strip()  # Remove leading and trailing characters
    return None


class Sasl:
    """
    A simple object to hold sasl mechanisms, sasl username and password
    """

    def __init__(self, mechs=None, user=None, password=None, sasl_password_file=None):
        self.mechs = mechs
        self.user = user
        self.password = password
        self.sasl_password_file = sasl_password_file
        if self.sasl_password_file:
            self.password = get_password(self.sasl_password_file)


def opts_url(opts):
    """Fix up default URL settings based on options"""
    url = Url(opts.bus)

    # If the options indicate SSL, make sure we use the amqps scheme.
    if opts.ssl_certificate or opts.ssl_trustfile or opts.bus.startswith("amqps:"):
        url.scheme = "amqps"
    return url


def opts_sasl(opts):
    url = Url(opts.bus)
    mechs, user, password, sasl_password_file = opts.sasl_mechanisms, (opts.sasl_username or url.username), (opts.sasl_password or url.password), opts.sasl_password_file

    if not (mechs or user or password or sasl_password_file):
        return None

    return Sasl(mechs, user, password, sasl_password_file)


def opts_ssl_domain(opts, mode=SSLDomain.MODE_CLIENT):
    """Return proton.SSLDomain from command line options or None if no SSL options specified.
    @param opts: Parsed optoins including connection_options()
    """

    url = opts_url(opts)
    if not url.scheme == "amqps":
        return None

    certificate, key, trustfile, password, password_file, ssl_disable_peer_name_verify = opts.ssl_certificate,\
        opts.ssl_key,\
        opts.ssl_trustfile,\
        opts.ssl_password,\
        opts.ssl_password_file, \
        opts.ssl_disable_peer_name_verify

    if password_file:
        password = get_password(password_file)

    domain = SSLDomain(mode)

    if trustfile:
        domain.set_trusted_ca_db(str(trustfile))

    if ssl_disable_peer_name_verify:
        domain.set_peer_authentication(SSLDomain.VERIFY_PEER)
    else:
        domain.set_peer_authentication(SSLDomain.VERIFY_PEER_NAME)

    if certificate:
        domain.set_credentials(str(certificate), str(key), str(password))
    return domain
