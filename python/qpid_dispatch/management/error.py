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
ManagementError exception class and subclasses, with status codes used by AMQP.
"""

from httplib import responses as STATUS_TEXT
from httplib import OK, NO_CONTENT, CREATED, \
    BAD_REQUEST, UNAUTHORIZED, FORBIDDEN, NOT_FOUND, INTERNAL_SERVER_ERROR, NOT_IMPLEMENTED

class ManagementError(Exception):
    """
    An AMQP management error.
    str() gives a string with status code and text.
    @ivar status: integer status code.
    @ivar description: detailed description of error.
    """
    def __init__(self, status, description):
        self.status, self.description = status, description
        super(ManagementError, self).__init__(description)

    @staticmethod
    def create(status, description):
        """Create the appropriate ManagementError subclass for status"""
        try:
            class_name = '' + (STATUS_TEXT[status].replace(' ',''))
            return globals()[class_name](description)
        except KeyError, e:
            return ManagementError(status, description)

def _error_class(code):
    class Error(ManagementError):
        def __init__(self, description): ManagementError.__init__(self, code, description)
    return Error

class BadRequest(_error_class(BAD_REQUEST)): pass
class Unauthorized(_error_class(UNAUTHORIZED)): pass
class Forbidden(_error_class(FORBIDDEN)): pass
class NotFound(_error_class(NOT_FOUND)): pass
class InternalServerError(_error_class(INTERNAL_SERVER_ERROR)): pass
class NotImplemented(_error_class(NOT_IMPLEMENTED)): pass
