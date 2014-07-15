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

# HTTP status codes used by AMQP (extend as needed)

from httplib import responses as STATUS_TEXT

STATUS_CODES=['OK', 'BAD_REQUEST', 'UNAUTHORIZED', 'FORBIDDEN', 'NOT_FOUND', 'REQUEST_TIMEOUT', 'INTERNAL_SERVER_ERROR', 'NOT_IMPLEMENTED']

for code in STATUS_CODES: exec("from httplib import %s" % code)

class ManagementError(Exception):
    """
    An AMQP management error.
    str() gives a string with status code and text.
    @ivar status: integer status code.
    @ivar description: detailed description of error.
    """
    def __init__(self, status, description):
        self.status, self.description = status, description

    def __str__(self):
        text = STATUS_TEXT.get(self.status) or "Unknown status %s"%self.status
        if text in self.description: return self.description
        else: return "%s: %s"%(text, self.description)

__all__ = STATUS_CODES + ['ManagementError', 'STATUS_TEXT']
