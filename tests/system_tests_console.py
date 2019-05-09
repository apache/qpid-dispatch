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
import errno
import re
import system_test
import unittest
from subprocess import PIPE
import subprocess
import shutil
from system_test import main_module, SkipIfNeeded

class ConsolePreReq(object):
    @staticmethod
    def get_dirs(remove):
        # expecting <base-path>/build/system_test.dir/system_tests_console/ConsoleTest/test_console
        cwd = os.getcwd()
        # l_base is the path that ends with qpid-dispatch's home dir
        l_base = cwd.split('/')[:-remove]
        l_test_cmd = '/'.join(l_base + ['build', 'console', 'node_modules', '.bin', 'mocha'])
        l_test_dir = '/'.join(l_base + ['console', 'stand-alone', 'test'])
        l_src_dir = '/'.join(l_base + ['console', 'stand-alone'])
        l_node_dir = '/'.join(l_base + ['console', 'stand-alone', 'node_modules'])
        return l_test_cmd, l_test_dir, l_src_dir, l_node_dir

    @staticmethod
    def is_cmd(name):
        ''' determine if a command is present and executes on the system '''
        try:
            devnull = open(os.devnull, "w")
            subprocess.Popen([name], stdout=devnull, stderr=devnull).communicate()
        except OSError as e:
            if e.errno == os.errno.ENOENT:
                return False
        return True

    @staticmethod
    def find_dirs():
        for i in range(6, 0, -1):
            (test_cmd, test_dir, src_dir, node_dir) = ConsolePreReq.get_dirs(i)
            if os.path.isdir(src_dir):
                return test_cmd, test_dir, src_dir, node_dir

        raise OSError(errno.ENOENT, os.strerror(errno.ENOENT), src_dir)

    @staticmethod
    def should_skip():
        try:
            (test_cmd, test_dir, src_dir, node_dir) = ConsolePreReq.find_dirs()
            found_npm = ConsolePreReq.is_cmd('npm')
            found_mocha = ConsolePreReq.is_cmd(test_cmd)

            return not found_npm or not found_mocha or not os.path.isdir(src_dir)
        except OSError:
            return True


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
        pret = 0
        (test_cmd, test_dir, src_dir, node_dir) = ConsolePreReq.find_dirs()

        out = ''
        ''' The console test needs a node_modules dir in the source directory.
            If the node_modules dir is not present in the source dir, create it.
            An alternative is to copy all the source files to the build/console dir. '''
        node_modules = os.path.isdir(node_dir)
        if not node_modules:
            p0 = subprocess.Popen(['npm', 'install', '--loglevel=error'], stdout=PIPE, cwd=src_dir)
            p0.wait()
            shutil.copy2("%s/package-lock.json" % src_dir, "%s/package-lock.json.tmp" % src_dir)

        prg = [test_cmd,'--require', 'babel-core/register', test_dir, '--http_port=%s' % self.http_port, '--src=%s/' % src_dir]
        p = self.popen(prg, stdout=PIPE, expect=None)
        out = p.communicate()[0]
        pret = p.returncode

        # write the output
        with open('run_console_test.out', 'w') as popenfile:
            popenfile.write('returncode was %s\n' % p.returncode)
            popenfile.write('out was:\n')
            popenfile.writelines(str(out))

        # if we created the node_modules dir, remove it
        if not node_modules:
            shutil.rmtree(node_dir)
            shutil.copy2("%s/package-lock.json.tmp" % src_dir, "%s/package-lock.json" % src_dir)
            os.remove("%s/package-lock.json.tmp" % src_dir)            

        assert pret == 0, \
            "console test exit status %s, output:\n%s" % (pret, out)
        return out

    # If we are unable to find the console's source directory,
    # or if we can't run the npm or mocha command. Skip the test
    @SkipIfNeeded(ConsolePreReq.should_skip(), 'Test skipped')
    def test_console(self):
        self.run_console_test()

if __name__ == '__main__':
    unittest.main(main_module())
