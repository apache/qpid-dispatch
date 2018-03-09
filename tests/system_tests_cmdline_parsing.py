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
This is a unit test class used to validate how qdrouterd
behaves with different command line arguments combinations,
in order to ensure it won't break, causing bad experiences
to the users.
"""

import os
import signal
from subprocess import PIPE, STDOUT
import unittest2 as unittest
from system_test import TestCase, Qdrouterd, main_module, Process

class CommandLineTest(TestCase):
    """
    System tests for command line arguments parsing
    """

    @classmethod
    def setUpClass(cls):
        """Uses a default config for testing"""

        super(CommandLineTest, cls).setUpClass()
        cls.name = "test-router"
        cls.config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'test-router'}),
            ('listener', {'port': cls.tester.get_port()})
        ])

    def run_qdrouterd_as_daemon(self, config_file_name="test-router",
                                pid_file_name=os.getcwd()+'/test.pid'):
        """
        Runs qdrouterd as a daemon, using the provided config_file_name
        in order to ensure router is able to load it, be it using a
        full or relative path.

        :param config_file_name: The configuration file name to be written
        :param pid_file_name: PID file name (must be full path)
        :return:
        """
        pipe = self.popen(
            ['qdrouterd', '-d', '-c',
             self.config.write(config_file_name), '-P', pid_file_name],
            stdout=PIPE, stderr=STDOUT, expect=Process.EXIT_OK)
        out = pipe.communicate()[0]

        try:
            pipe.teardown()
            # kill qdrouterd running as a daemon
            with open(pid_file_name, 'r') as pidfile:
                for line in pidfile:
                    os.kill(int(line), signal.SIGTERM)
            pidfile.close()
        except OSError as ex:
            raise Exception("%s\n%s" % (ex, out))


    def test_01_config_relative_path(self):
        """
        Starts qdrouterd as daemon, enforcing a config file name with
        relative path.
        """

        try:
            self.run_qdrouterd_as_daemon()
        except OSError as ex:
            self.fail(ex)

    def test_02_config_full_path(self):
        """
        Starts qdrouterd as daemon, enforcing a config file name with
        full path.
        """

        try:
            self.run_qdrouterd_as_daemon(os.getcwd() + "/test-router.conf")
        except OSError as ex:
            self.fail(ex)


if __name__ == '__main__':
    unittest.main(main_module())
