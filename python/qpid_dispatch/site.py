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

"""Qpid Dispatch site configuration - INTERNAL USE ONLY"""

import os, sys

HOME = os.environ.get('QPID_DISPATCH_HOME')

if not HOME:
    try:
        from .site_data import HOME
    except ImportError, e:
        raise ImportError("%s: Set QPID_DISPATCH_HOME environment variable." % e)

sys.path.insert(0, os.path.join(HOME, 'python'))
