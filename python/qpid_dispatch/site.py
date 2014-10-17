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
INTERNAL USE ONLY - Qpid Dispatch site configuration.
"""

import os, sys

def get_variable(name):
    """Get variable value  by first checking os.environ, then site_data"""
    value = os.environ.get(name)
    if value: return value
    site_data = __import__('qpid_dispatch.site_data', globals(), locals(), [name])
    return getattr(site_data, name)

QPID_DISPATCH_HOME = get_variable('QPID_DISPATCH_HOME')
QPID_DISPATCH_LIB = get_variable('QPID_DISPATCH_LIB')

sys.path.insert(0, os.path.join(QPID_DISPATCH_HOME, 'python'))
