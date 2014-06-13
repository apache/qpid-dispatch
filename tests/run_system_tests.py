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
Runs all scripts named system_tests_*.py in the same directory as this script.
Note that each system test is an executable script, you can run them directly.
"""

import os
import sys
import re
import unittest

# Collect all system_tests_*.py scripts
test_dir = os.path.normpath(os.path.dirname(__file__))
os.environ.setdefault('QPID_DISPATCH_HOME', os.path.dirname(test_dir))
tests = [[f] for f in os.listdir(test_dir) if re.match('^system_tests.*.py$', f)]

# Tests to re-run with extra parameters
tests += [['system_tests_two_routers.py', '--ssl']]

status = 0

def run_test(script, *args):
    global status
    cmd = "%s %s -v %s"%(sys.executable, os.path.join(test_dir,script), " ".join(args))
    sys.stderr.write("\nRunning %s\n"%cmd)
    if os.system(cmd) != 0:
        status = 1

for test in tests:
    run_test(*test)

sys.exit(status)
