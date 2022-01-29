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

Run without any environment settings, this will test a dispatch router installed in
the standard system places. Use run.py or config.sh to run against dispatch build.
"""
import os
import sys
from fnmatch import fnmatch
import system_test
from system_test import unittest

# Collect all system_tests_*.py scripts in the same directory as this script.
test_modules = [os.path.splitext(f)[0] for f in os.listdir(system_test.DIR)
                if fnmatch(f, "system_tests_*.py")]
sys.path = [system_test.DIR] + sys.path  # Find test modules in sys.path

# python < 2.7 unittest main won't load tests from modules, so use the loader:
all_tests = unittest.TestSuite()
for m in test_modules:
    tests = unittest.defaultTestLoader.loadTestsFromModule(__import__(m))
    all_tests.addTest(tests)
result = unittest.TextTestRunner(verbosity=2).run(all_tests)
sys.exit(not result.wasSuccessful())
