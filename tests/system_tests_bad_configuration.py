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
Ensure router continues to work when configuration has some configurations,
that might cause problems, or caused issues in the past.
For example, unresolvable host names.
"""

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function


from threading import Timer
import os
import re
from subprocess import PIPE, STDOUT
from system_test import TestCase, Qdrouterd, TIMEOUT, Process


class RouterTestBadConfiguration(TestCase):

    """
    This test case sets up a router using configurations that are not
    well defined, but are not supposed to cause a crash to the router
    process.
    """
    @classmethod
    def setUpClass(cls):
        """
        Set up router instance configuration to be used for testing.
        :return:
        """
        super(RouterTestBadConfiguration, cls).setUpClass()
        cls.name = "test-router"
        cls.config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR.A'}),
            # Define a connector that uses an unresolvable hostname
            ('connector',
             {'name': 'UnresolvableConn',
              'host': 'unresolvable.host.name',
              'port': 'amqp'}),
            ('listener',
             {'port': cls.tester.get_port()}),
        ])

        try:
            cls.router = cls.tester.qdrouterd(cls.name, cls.config, wait=False)
        except OSError:
            pass

    def __init__(self, test_method):
        TestCase.__init__(self, test_method)
        self.error_caught = False
        self.timer_delay = 1
        self.max_attempts = 20
        self.attempts_made = 0
        self.schedule_timer()

    def schedule_timer(self):
        """
        Schedules a timer triggers wait_for_unresolvable_host after
        timer_delay has been elapsed.
        :return:
        """
        Timer(self.timer_delay, self.wait_for_unresolvable_host).start()

    @classmethod
    def tearDownClass(cls):
        super(RouterTestBadConfiguration, cls).tearDownClass()

    def address(self):
        """
        Returns the address that can be used along with qdmanage
        to query the running instance of the dispatch router.
        :return:
        """
        return self.router.addresses[0]

    def waiting_for_error(self):
        """
        Returns True if max_attempts not yet reached and error is still not found.
        :return: bool
        """
        return not self.error_caught and self.attempts_made < self.max_attempts

    def wait_for_unresolvable_host(self):
        """
        Wait for error to show up in the logs based on pre-defined max_attempts
        and timer_delay. If error is not caught within max_attempts * timer_delay
        then it stops scheduling new attempts.
        :return:
        """
        with open('../setUpClass/test-router.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            expected_errors = [
                "proton:io Name or service not known",  # Linux
                "proton:io unknown node or service",  # macOS
            ]
            errors_caught = [line for line in log_lines
                             if any([expected_error in line for expected_error in expected_errors])]

            self.error_caught = any(errors_caught)

            # If condition not yet satisfied and not exhausted max attempts,
            # re-schedule the verification.
            if self.waiting_for_error():
                self.attempts_made += 1
                self.schedule_timer()

    def setUp(self):
        """
        Causes tests to wait till timer has found the expected error or
        after it times out.
        :return:
        """
        # Wait till error is found or timed out waiting for it.
        while self.waiting_for_error():
            pass

    def test_unresolvable_host_caught(self):
        """
        Validate if the error message stating host is unresolvable is printed
        to the router log.
        It expects that the error can be caught in the logs.
        :return:
        """
        self.assertTrue(self.error_caught)

    def test_qdmanage_query(self):
        """
        Attempts to query the router after the error (or timeout) has occurred.
        It expects a successful query response to be returned by the router.
        :return:
        """
        p = self.popen(
            ['qdmanage', '-b', self.address(), 'query', '--type=router', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=Process.EXIT_OK,
            universal_newlines=True)
        out = p.communicate()[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception("%s\n%s" % (e, out))
        return out


class RouterTestIdFailCtrlChar(TestCase):
    """
    This test case sets up a router using a configuration router id
    that is illegal (control character). The router should not start.
    """
    @classmethod
    def setUpClass(cls):
        super(RouterTestIdFailCtrlChar, cls).setUpClass()
        cls.name = "test-router-ctrl-char"

    def __init__(self, test_method):
        TestCase.__init__(self, test_method)

    @classmethod
    def tearDownClass(cls):
        super(RouterTestIdFailCtrlChar, cls).tearDownClass()

    def test_verify_reject_id_with_ctrl_char(self):
        """
        Writes illegal config, runs router, examines console output
        """
        conf_path = "../setUpClass/test-router-ctrl-char.conf"
        with open(conf_path, 'w') as router_conf:
            router_conf.write("router { \n")
            router_conf.write("    id: abc\\bdef \n")
            router_conf.write("}")
        lib_include_path = os.path.join(os.environ["QPID_DISPATCH_HOME"], "python")
        p = self.popen(
            ['qdrouterd', '-c', conf_path, '-I', lib_include_path],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=Process.EXIT_FAIL,
            universal_newlines=True)
        out = p.communicate(timeout=5)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception("%s\n%s" % (e, out))
        if "AttributeError" not in out:
            print("output: ", out)
            assert False, "AttributeError not in process output"

class RouterTestIdFailWhiteSpace(TestCase):
    """
    This test case sets up a router using a configuration router id
    that is illegal (whitespace character). The router should not start.
    """
    @classmethod
    def setUpClass(cls):
        super(RouterTestIdFailWhiteSpace, cls).setUpClass()
        cls.name = "test-router-ctrl-char"

    def __init__(self, test_method):
        TestCase.__init__(self, test_method)

    @classmethod
    def tearDownClass(cls):
        super(RouterTestIdFailWhiteSpace, cls).tearDownClass()

    def test_verify_reject_id_with_whitespace(self):
        """
        Writes illegal config, runs router, examines console output
        """
        conf_path = "../setUpClass/test-router-whitespace.conf"
        with open(conf_path, 'w') as router_conf:
            router_conf.write("router { \n")
            router_conf.write("    id: abc def \n")
            router_conf.write("}")
        lib_include_path = os.path.join(os.environ["QPID_DISPATCH_HOME"], "python")
        p = self.popen(
            ['qdrouterd', '-c', conf_path, '-I', lib_include_path],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=Process.EXIT_FAIL,
            universal_newlines=True)
        out = p.communicate(timeout=5)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception("%s\n%s" % (e, out))
        if "AttributeError" not in out:
            print("output: ", out)
            assert False, "AttributeError not in process output"
