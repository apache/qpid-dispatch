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

"""Common options for qpid dispatch tools"""
from optparse import OptionGroup

def connection_options(options, title="Connection Options"):
    """Return an OptionGroup for connection options."""
    group = OptionGroup(options, title)
    group.add_option("-b", "--bus", action="store", type="string", default="0.0.0.0",
                     metavar="<url>", help="URL of the messaging bus to connect to (default %default)")
    group.add_option("-r", "--router", action="store", type="string", default=None,
                     metavar="<router-id>", help="Router to be queried")
    group.add_option("-t", "--timeout", action="store", type="float", default=5, metavar="<secs>",
                      help="Maximum time to wait for connection in seconds (default %default)")
    group.add_option("--sasl-mechanism", action="store", type="string", metavar="<mech>",
                      help="SASL mechanism for authentication (e.g. EXTERNAL, ANONYMOUS, PLAIN, CRAM-MD5, DIGEST-MD5, GSSAPI). SASL automatically picks the most secure available mechanism - use this option to override.")
    group.add_option("--ssl-certificate", action="store", type="string", metavar="<cert>",
                     help="Client SSL certificate (PEM Format)")
    group.add_option("--ssl-key", action="store", type="string", metavar="<key>",
                     help="Client SSL private key (PEM Format)")
    return group
