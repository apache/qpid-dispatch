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

"""Mock implementation of the dispatch C extension module for use in unit tests."""

LOG_TRACE    = 1
LOG_DEBUG    = 2
LOG_INFO     = 4
LOG_NOTICE   = 8
LOG_WARNING  = 16
LOG_ERROR    = 32
LOG_CRITICAL = 64
LOG_STACK_LIMIT = 8

TREATMENT_MULTICAST_FLOOD  = 0
TREATMENT_MULTICAST_ONCE   = 1
TREATMENT_ANYCAST_CLOSEST  = 2
TREATMENT_ANYCAST_BALANCED = 3
TREATMENT_LINK_BALANCED    = 4


class LogAdapter:

    def __init__(self, mod_name):
        self.mod_name = mod_name

    def log(self, level, text):
        print("LOG: mod=%s level=%d text=%s" % (self.mod_name, level, text))


class IoAdapter:

    def __init__(self, handler, address, global_address=False):
        self.handler = handler
        self.address = address
        self.global_address = global_address

    def send(self, address, properties, application_properties, body, correlation_id=None):
        print("IO: send(addr=%s properties=%r application_properties=%r body=%r"
              % (address, properties, application_properties, body))
