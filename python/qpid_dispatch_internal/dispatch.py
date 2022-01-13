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

"""Interface between python and libqpid-dispatch.so.

This module contains python ctypes definitions to directly call functions in the
libqpid-dispatch.so library from python.

The C library also adds the following C extension types to this module:

- LogAdapter: Logs to the C logging system.
- IoAdapter: Receives messages from the router into python.

This module also prevents the proton python module from being accidentally loaded.
"""
import builtins
import ctypes
import sys
from ctypes import c_char_p, c_long, py_object


class CError(Exception):
    """Exception raised if there is an error in a C call"""
    pass


class QdDll(ctypes.PyDLL):
    """
    Load the library, set up function prototypes.

    NOTE: We use the python calling convention because the C library
    internally makes python calls.
    """

    def __init__(self, handle):
        super(QdDll, self).__init__("qpid-dispatch", handle=handle)

        # Types
        self.qd_dispatch_p = ctypes.c_void_p

        # No check on qd_error_* functions, it would be recursive
        self._prototype(self.qd_error_code, c_long, [], check=False)
        self._prototype(self.qd_error_message, c_char_p, [], check=False)
        self._prototype(self.qd_log_entity, c_long, [py_object])
        self._prototype(self.qd_dispatch_configure_router, None, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_prepare, None, [self.qd_dispatch_p])
        self._prototype(self.qd_dispatch_configure_listener, ctypes.c_void_p, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_configure_connector, ctypes.c_void_p, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_configure_ssl_profile, ctypes.c_void_p, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_configure_sasl_plugin, ctypes.c_void_p, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_connection_manager_delete_listener, None, [self.qd_dispatch_p, ctypes.c_void_p])
        self._prototype(self.qd_connection_manager_delete_connector, None, [self.qd_dispatch_p, ctypes.c_void_p])
        self._prototype(self.qd_connection_manager_delete_ssl_profile, ctypes.c_bool, [self.qd_dispatch_p, ctypes.c_void_p])
        self._prototype(self.qd_connection_manager_delete_sasl_plugin, ctypes.c_bool, [self.qd_dispatch_p, ctypes.c_void_p])

        self._prototype(self.qd_dispatch_configure_address, None, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_configure_link_route, None, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_configure_auto_link, None, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_configure_exchange, None, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_configure_binding, None, [self.qd_dispatch_p, py_object])

        self._prototype(self.qd_dispatch_configure_policy, None, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_register_policy_manager, None, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_policy_c_counts_alloc, c_long, [], check=False)
        self._prototype(self.qd_dispatch_policy_c_counts_free, None, [c_long], check=False)
        self._prototype(self.qd_dispatch_policy_c_counts_refresh, None, [c_long, py_object])
        self._prototype(self.qd_dispatch_policy_host_pattern_add, ctypes.c_bool, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_policy_host_pattern_remove, None, [self.qd_dispatch_p, py_object])
        self._prototype(self.qd_dispatch_policy_host_pattern_lookup, c_char_p, [self.qd_dispatch_p, py_object])

        self._prototype(self.qd_dispatch_register_display_name_service, None, [self.qd_dispatch_p, py_object])

        self._prototype(self.qd_dispatch_set_agent, None, [self.qd_dispatch_p, py_object])

        self._prototype(self.qd_router_setup_late, None, [self.qd_dispatch_p])

        self._prototype(self.qd_dispatch_router_lock, None, [self.qd_dispatch_p])
        self._prototype(self.qd_dispatch_router_unlock, None, [self.qd_dispatch_p])

        self._prototype(self.qd_connection_manager_start, None, [self.qd_dispatch_p])
        self._prototype(self.qd_entity_refresh_begin, c_long, [py_object])
        self._prototype(self.qd_entity_refresh_end, None, [])

        self._prototype(self.qd_log_recent_py, py_object, [c_long])

    def _prototype(self, f, restype, argtypes, check=True):
        """Set up the return and argument types and the error checker for a
        ctypes function"""

        def _do_check(result, func, args):
            if check and self.qd_error_code():
                raise CError(self.qd_error_message())
            if restype is c_char_p and result:
                # in python3 c_char_p returns a byte type for the error
                # message. We need to convert that to a string
                result = result.decode('utf-8')
            return result

        f.restype = restype
        f.argtypes = argtypes
        f.errcheck = _do_check
        return f

    def function(self, fname, restype, argtypes, check=True):
        return self._prototype(getattr(self, fname), restype, argtypes, check)


# Prevent accidental loading of the proton python module inside dispatch.
# The proton-C library is linked with the dispatch C library, loading the proton
# python module loads a second copy of the library and mayhem ensues.
#
# Note the FORBIDDEN list is over-written to disable this tests in mock python
# testing code.
FORBIDDEN = ["proton"]


def check_forbidden():
    bad = set(FORBIDDEN) & set(sys.modules)
    if bad:
        raise ImportError("Forbidden modules loaded: '%s'." % "', '".join(bad))


def import_check(name, *args, **kw):
    if name in FORBIDDEN:
        raise ImportError("Python code running inside a dispatch router cannot import '%s', use the 'dispatch' module for internal messaging" % name)
    return builtin_import(name, *args, **kw)


check_forbidden()

builtin_import = builtins.__import__
builtins.__import__ = import_check
