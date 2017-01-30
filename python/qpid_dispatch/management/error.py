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

#from httplib import responses as STATUS_TEXT
#from httplib import OK, NO_CONTENT, CREATED, \
#    BAD_REQUEST, UNAUTHORIZED, FORBIDDEN, NOT_FOUND, INTERNAL_SERVER_ERROR, NOT_IMPLEMENTED

OK = 200
NO_CONTENT = 204
CREATED = 201
BAD_REQUEST = 400
UNAUTHORIZED = 401
FORBIDDEN = 403
NOT_FOUND = 404
INTERNAL_SERVER_ERROR = 500
NOT_IMPLEMENTED = 501

# Mapping status codes to official W3C names
STATUS_TEXT = {
    100: 'Continue',
    101: 'Switching Protocols',

    200: 'OK',
    201: 'Created',
    202: 'Accepted',
    203: 'Non-Authoritative Information',
    204: 'No Content',
    205: 'Reset Content',
    206: 'Partial Content',

    300: 'Multiple Choices',
    301: 'Moved Permanently',
    302: 'Found',
    303: 'See Other',
    304: 'Not Modified',
    305: 'Use Proxy',
    306: '(Unused)',
    307: 'Temporary Redirect',

    400: 'Bad Request',
    401: 'Unauthorized',
    402: 'Payment Required',
    403: 'Forbidden',
    404: 'Not Found',
    405: 'Method Not Allowed',
    406: 'Not Acceptable',
    407: 'Proxy Authentication Required',
    408: 'Request Timeout',
    409: 'Conflict',
    410: 'Gone',
    411: 'Length Required',
    412: 'Precondition Failed',
    413: 'Request Entity Too Large',
    414: 'Request-URI Too Long',
    415: 'Unsupported Media Type',
    416: 'Requested Range Not Satisfiable',
    417: 'Expectation Failed',

    500: 'Internal Server Error',
    501: 'Not Implemented',
    502: 'Bad Gateway',
    503: 'Service Unavailable',
    504: 'Gateway Timeout',
    505: 'HTTP Version Not Supported',
}


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
            class_name = STATUS_TEXT[status].replace(' ', '') + "Status"
            return globals()[class_name](description)
        except KeyError:
            return ManagementError(status, description)

def _error_class(status):
    """Create a ManagementError class for a particular status"""
    class Error(ManagementError):
        def __init__(self, description): ManagementError.__init__(self, status, description)
    return Error

class BadRequestStatus(_error_class(BAD_REQUEST)): pass
class UnauthorizedStatus(_error_class(UNAUTHORIZED)): pass
class ForbiddenStatus(_error_class(FORBIDDEN)): pass
class NotFoundStatus(_error_class(NOT_FOUND)): pass
class InternalServerErrorStatus(_error_class(INTERNAL_SERVER_ERROR)): pass
class NotImplementedStatus(_error_class(NOT_IMPLEMENTED)): pass

__all__ = [
    u"STATUS_TEXT", u"OK", u"NO_CONTENT", u"CREATED",
    u"BAD_REQUEST", u"UNAUTHORIZED", u"FORBIDDEN", u"NOT_FOUND",
    u"INTERNAL_SERVER_ERROR", u"NOT_IMPLEMENTED",
    u"ManagementError",
    u"BadRequestStatus", u"UnauthorizedStatus", u"ForbiddenStatus",
    u"NotFoundStatus", u"InternalServerErrorStatus", u"NotImplementedStatus"]
