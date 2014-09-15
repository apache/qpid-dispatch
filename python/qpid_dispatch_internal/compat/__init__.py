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

"""Compatibility hacks for older versions of python"""

import sys

__all__ = ["OrderedDict"]

try: from collections import OrderedDict
except: from ordereddict import OrderedDict

if sys.version_info >= (2, 7):
    json_load_kwargs = {'object_pairs_hook':OrderedDict}
else:
    json_load_kwargs = {}
