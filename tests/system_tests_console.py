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

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

import os
import re
import system_test
import unittest
from subprocess import PIPE
import subprocess
import shutil
from proton import Url, SSLDomain, SSLUnavailable, SASL
from system_test import main_module

class ConsoleTest(system_test.TestCase):
    """Run console tests"""
    @classmethod
    def setUpClass(cls):
        super(ConsoleTest, cls).setUpClass()
        config = system_test.Qdrouterd.Config([
            ('router', {'id': 'QDR.A', 'workerThreads': 1}),
            ('listener', {'port': cls.tester.get_port()}),
        ])
        cls.router = cls.tester.qdrouterd('test-router', config)

    def run_console_test(self):
        cwd = os.getcwd()   # expecting <base-path>/build/system_test.dir/system_tests_console/ConsoleTest/test_console
        base = cwd.split('/')[:-6]   #path that ends with qpid-dispatch's home dir
        test_cmd = '/'.join(base + ['build', 'console', 'node_modules', '.bin', 'mocha'])
        test_dir = '/'.join(base + ['console', 'stand-alone', 'test'])
        src_dir = '/'.join(base + ['console', 'stand-alone'])

        # The console test needs a node_modules dir in the source directory
        # If the node_modules dir is not present in the source dir, create it.
        # An alternative is to copy all the source files to the build/console dir.
        node_dir = '/'.join(base + ['console', 'stand-alone', 'node_modules'])
        node_modules = os.path.isdir(node_dir)
        if not node_modules:
            p0 = subprocess.Popen(['npm', 'install', '--loglevel=error'], stdout=PIPE, cwd=src_dir)
            p0.wait();

        prg = [test_cmd,'--require', 'babel-core/register', test_dir, '--src=%s/' % src_dir]
        p = self.popen(prg, stdout=PIPE, expect=None)
        out = p.communicate()[0]

        # write the output
        with open('run_console_test.out', 'w') as popenfile:
            popenfile.write('returncode was %s\n' % p.returncode)
            popenfile.write('out was:\n')
            popenfile.writelines(out)

        # if we created the node_modules dir, remove it
        if not node_modules:
            shutil.rmtree(node_dir)

        assert p.returncode == 0, \
            "console test exit status %s, output:\n%s" % (p.returncode, out)
        return out

    def test_console(self):
        self.run_console_test()

if __name__ == '__main__':
    unittest.main(main_module())
