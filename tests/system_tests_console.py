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
        cls.http_port = cls.tester.get_port()
        config = system_test.Qdrouterd.Config([
            ('router', {'id': 'QDR.A', 'workerThreads': 1}),
            ('listener', {'port': cls.http_port, 'http': True}),
        ])
        cls.router = cls.tester.qdrouterd('test-router', config)

    def run_console_test(self):
        # expecting <base-path>/build/system_test.dir/system_tests_console/ConsoleTest/test_console
        # /foo/qpid-dispatch/build/system_test.dir/system_tests_console/ConsoleTest/test_console/
        # run_console_test.out
        cwd = os.getcwd()

        def get_base(remove):
            l_base = cwd.split('/')[:-remove]   # path that ends with qpid-dispatch's home dir
            l_test_cmd = '/'.join(l_base + ['build', 'console', 'node_modules', '.bin', 'mocha'])
            l_test_dir = '/'.join(l_base + ['console', 'stand-alone', 'test'])
            l_src_dir = '/'.join(l_base + ['console', 'stand-alone'])
            return l_base, l_test_cmd, l_test_dir, l_src_dir
        
        (base, test_cmd, test_dir, src_dir) = get_base(6)
        found_src = os.path.isdir(src_dir)
        # running the test from the command line results in a different path
        if not found_src:
            (base, test_cmd, test_dir, src_dir) = get_base(5)
            found_src = os.path.isdir(src_dir)

        pret = 0
        out = 'Skipped'
        if found_src:  # if we are unable to find the console's source directory. Skip the test
            # The console test needs a node_modules dir in the source directory
            # If the node_modules dir is not present in the source dir, create it.
            # An alternative is to copy all the source files to the build/console dir.
            node_dir = '/'.join(base + ['console', 'stand-alone', 'node_modules'])
            node_modules = os.path.isdir(node_dir)
            if not node_modules:
                p0 = subprocess.Popen(['npm', 'install', '--loglevel=error'], stdout=PIPE, cwd=src_dir)
                p0.wait();

            prg = [test_cmd,'--require', 'babel-core/register', test_dir, '--http_port=%s' % self.http_port, '--src=%s/' % src_dir]
            p = self.popen(prg, stdout=PIPE, expect=None)
            out = p.communicate()[0]
            pret = p.returncode

            # write the output
            with open('run_console_test.out', 'w') as popenfile:
                popenfile.write('returncode was %s\n' % p.returncode)
                popenfile.write('out was:\n')
                popenfile.writelines(out)

            # if we created the node_modules dir, remove it
            if not node_modules:
                shutil.rmtree(node_dir)

        assert pret == 0, \
            "console test exit status %s, output:\n%s" % (pret, out)
        return out

    def test_console(self):
        self.run_console_test()


if __name__ == '__main__':
    unittest.main(main_module())
