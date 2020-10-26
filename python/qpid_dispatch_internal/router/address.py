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

"""Parse & decompose router addresses"""

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

class Address(str):
    """A router address. Immutable, hashable.
    Subclasses str, so inherits all hash, comparison etc. properties.
    Provides a central place for logic to construct addresses of various types.
    """

    AMQP="amqp:"
    TOPO="_topo"

    def __new__(self, addr): # Subclassing immutable type, must use __new__ not __init__
        if addr.startswith(self.AMQP):
            return str.__new__(addr)
        else:
            return str.__new__(Address, "%s/%s" % (self.AMQP, addr))

    @classmethod
    def mobile(cls, path):
        """Create a mobile address, can be moved to and referenced from anywhere in the network.
        @param path: The mobile address string.
        """
        return Address(path)

    @classmethod
    def topological(cls, router_id, path=None, area=None):
        """Create a topological address, references a specific router.
        @param router_id: ID of target router.
        @param path: Path part of address.
        @param area: Routing area (placeholder)
        """
        addr = "%s/%s/%s" % (cls.TOPO, area, router_id)
        if path:
            addr = "%s/%s" % (addr, path)
        return Address(addr)

    def __repr__(self): return "Address(%r)" % str(self)
