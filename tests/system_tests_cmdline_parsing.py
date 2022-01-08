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
from system_test import TestCase, Qdrouterd, main_module, Process, wait_port
from system_test import unittest


class CommandLineTest(TestCase):
    """System tests for command line arguments parsing"""
    testport = 0
    testname = ""

    @classmethod
    def setUpClass(cls):
        """Uses a default config for testing"""

        super(CommandLineTest, cls).setUpClass()
        cls.name = "test-router-1"
        CommandLineTest.testname = cls.name
        CommandLineTest.testport = cls.tester.get_port()
        cls.config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': CommandLineTest.name}),
            ('listener', {'port': CommandLineTest.testport}),
            ('log', {'module': 'DEFAULT', 'enable': 'trace+', 'includeSource': 'true', 'outputFile': os.getcwd() + "/" + CommandLineTest.name + '.log'})
        ])

    def run_qdrouterd_as_daemon(self, config_file_name, pid_file_name):
        """
        Runs qdrouterd as a daemon, using the provided config_file_name
        in order to ensure router is able to load it, be it using a
        full or relative path.

        :param config_file_name: The configuration file name to be written
        :param pid_file_name: PID file name (must be full path)
        :return:
        """
        pipe = self.popen(
            [os.path.join(os.environ.get('BUILD_DIR'), 'router', 'qdrouterd'), '-d',
             '-I', os.path.join(os.environ.get('SOURCE_DIR'), 'python'),
             '-c', self.config.write(config_file_name), '-P', pid_file_name],
            stdout=PIPE, stderr=STDOUT, expect=Process.EXIT_OK,
            universal_newlines=True)
        out = pipe.communicate()[0]
        wait_port(CommandLineTest.testport)

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
            self.run_qdrouterd_as_daemon("test-router", os.getcwd() + '/test.pid')
        except OSError as ex:
            self.fail(ex)


class CommandLineTest2(TestCase):
    """System tests for command line arguments parsing"""
    testport = 0
    testname = ""

    @classmethod
    def setUpClass(cls):
        """Uses a default config for testing"""

        super(CommandLineTest2, cls).setUpClass()
        cls.name = "test-router-2"
        CommandLineTest2.testname = cls.name
        CommandLineTest2.testport = cls.tester.get_port()
        # output has been deprecated. We are using it here to test backward compatibility.
        cls.config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': CommandLineTest2.testname}),
            ('listener', {'port': CommandLineTest2.testport}),
            ('log', {'module': 'DEFAULT', 'enable': 'trace+', 'includeSource': 'true', 'output': os.getcwd() + "/" + CommandLineTest2.name + '.log'})
        ])

    def run_qdrouterd_as_daemon(self, config_file_name, pid_file_name):
        """
        Runs qdrouterd as a daemon, using the provided config_file_name
        in order to ensure router is able to load it, be it using a
        full or relative path.

        :param config_file_name: The configuration file name to be written
        :param pid_file_name: PID file name (must be full path)
        :return:
        """
        pipe = self.popen(
            [os.path.join(os.environ.get('BUILD_DIR'), 'router', 'qdrouterd'), '-d',
             '-I', os.path.join(os.environ.get('SOURCE_DIR'), 'python'),
             '-c', self.config.write(config_file_name), '-P', pid_file_name],
            stdout=PIPE, stderr=STDOUT, expect=Process.EXIT_OK,
            universal_newlines=True)
        out = pipe.communicate()[0]
        wait_port(CommandLineTest2.testport)

        try:
            pipe.teardown()
            # kill qdrouterd running as a daemon
            with open(pid_file_name, 'r') as pidfile:
                for line in pidfile:
                    os.kill(int(line), signal.SIGTERM)
            pidfile.close()
        except OSError as ex:
            raise Exception("%s\n%s" % (ex, out))

    def test_02_config_full_path(self):
        """Starts qdrouterd as daemon, enforcing a config file name with full path."""

        try:
            self.run_qdrouterd_as_daemon(os.getcwd() + "/test-router-2.conf",
                                         pid_file_name=os.getcwd() + '/test.pid')
        except OSError as ex:
            self.fail(ex)


if __name__ == '__main__':
    unittest.main(main_module())
