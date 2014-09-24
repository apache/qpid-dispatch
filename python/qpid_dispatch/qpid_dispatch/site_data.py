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

"""
INTERNAL USE ONLY - Installed locations for qpid dispatch.

Do not import directly, import site.py which also checks for override by
environment variables. This file will not be available for uninstalled builds,
site.py will use env variables instead.
"""

from os.path import join

QPID_DISPATCH_HOME = ""
QPID_DISPATCH_LIB = ""
