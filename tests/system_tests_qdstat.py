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

import re
import system_test
import unittest
from subprocess import PIPE

class QdstatTest(system_test.TestCase):
    """Test qdstat tool output"""

    @classmethod
    def setUpClass(cls):
        super(QdstatTest, cls).setUpClass()
        config = system_test.Qdrouterd.Config([
            ('listener', {'port': cls.tester.get_port()}),
        ])
        cls.router = cls.tester.qdrouterd('test-router', config)

    def address(self): return self.router.hostports[0] # Overridden by subclasses

    def run_qdstat(self, args, regexp=None):
        p = self.popen(['qdstat', '--bus', self.address()] + args,
                       name='qdstat-'+self.id(), stdout=PIPE, expect=None)
        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)
        if regexp: assert re.search(regexp, out, re.I), "Can't find '%s' in '%s'" % (regexp, out)
        return out

    def test_help(self):
        self.run_qdstat(['--help'], r'Usage: qdstat')

    def test_general(self):
        self.run_qdstat(['--general'], r'(?s)Router Statistics.*Mode\s*Standalone')

    def test_connections(self):
        self.run_qdstat(['--connections'], r'state.*host.*container')

    def test_links(self):
        self.run_qdstat(['--links'], r'endpoint.*out.*local.*temp.')

    def test_nodes(self):
        self.run_qdstat(['--nodes'], r'router-id\s+next-hop\s+link')

    def test_address(self):
        self.run_qdstat(['--address'], r'\$management')

    def test_memory(self):
        self.run_qdstat(['--memory'], r'qd_address_t\s+[0-9]+')


class OldQdstatTest(QdstatTest):
    """Test with old managment interface"""
    def address(self): return super(OldQdstatTest, self).address() + '/$cmanagement'

if __name__ == '__main__':
    unittest.main(system_test.main_module())
