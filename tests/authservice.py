#!/usr/bin/env python3
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
# under the License.
#

import optparse
import signal
import sys
import os

from cproton import pn_sasl_config_path
from proton import Array, Data, symbol, UNDESCRIBED
from proton.handlers import MessagingHandler
from proton.reactor import Container
from proton import SSLDomain

import system_test


class AuthService(MessagingHandler):

    def __init__(self, address):
        super(AuthService, self).__init__()
        self.address = address
        self.permissions = {}
        self.allow('admin', '*', ['send', 'recv'])
        self.allow('guest', 'foo', ['send', 'recv'])
        self.listener = None
        self.tmo = 0.1  # seconds
        self.stop_req = False
        self.acceptor = None
        self.ssl_domain = SSLDomain(SSLDomain.MODE_SERVER)
        self.ssl_domain.set_credentials(self.ssl_file('server-certificate.pem'),
                                        self.ssl_file('server-private-key.pem'),
                                        password="server-password")
        self.ssl_domain.set_trusted_ca_db(self.ssl_file('ca-certificate.pem'))

    def ssl_file(self, name):
        return os.path.join(system_test.DIR, 'ssl_certs', name)

    def allow(self, user, address, permissions):
        if not self.permissions.get(user):
            self.permissions[user] = {}
        self.permissions[user][address] = Array(UNDESCRIBED, Data.STRING, *permissions)

    def on_start(self, event):
        self.listener = event.container.listen(self.address, ssl_domain=self.ssl_domain)
        event.container.schedule(self.tmo, self)

    def stop(self):
        self.stop_req = True

    def on_connection_opening(self, event):
        if self.permissions.get(event.transport.user):
            event.connection.properties = {
                symbol('authenticated-identity'): "%s" % event.transport.user,
                symbol('address-authz'): self.permissions[event.transport.user]
            }
        else:
            event.connection.properties = {
                symbol('authenticated-identity'): "%s" % event.transport.user,
                symbol('address-authz'): {}
            }

    def on_timer_task(self, event):
        if self.stop_req:
            if self.listener:
                self.listener.close()
            else:
                sys.exit(0)
        else:
            event.reactor.schedule(self.tmo, self)


parser = optparse.OptionParser(usage="usage: %prog [options]",
                               description="test authentication and authorization service")
parser.add_option("-a", "--address", default="localhost:55671",
                  help="address to listen on (default %default)")
parser.add_option("-c", "--config", help="sasl config path")
opts, args = parser.parse_args()

print('starting')
if opts.config:
    pn_sasl_config_path(None, opts.config)
    print('set sasl config path to %s' % opts.config)

handler = AuthService(opts.address)


def sigterm_handler(_signo, _stack_frame):
    handler.stop()


signal.signal(signal.SIGTERM, sigterm_handler)

Container(handler).run()
