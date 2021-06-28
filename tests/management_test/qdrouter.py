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

# pylint: disable=wildcard-import,unused-wildcard-import,missing-docstring,too-many-public-methods

import unittest
from qpid_dispatch_internal.management.config import Config

# Dash-separated configuration file
conf_text = """
# Line comment
router {
    id: QDR
    mode: standalone            # End of line comment
}
ssl-profile {
    name: test-profile
    password: secret
}
listener {
    name: l0
    saslMechanisms: ANONYMOUS
    sslProfile: test-profile
}
listener {
    identity: l1
    saslMechanisms: ANONYMOUS
    port: 1234
}
listener {
    saslMechanisms: ANONYMOUS
    port: 4567
}
"""

# camelCase configuration file
confText = """
# Line comment
router {
    id: QDR
    mode: standalone            # End of line comment
}
sslProfile {
    name: test-profile
    password: secret
}
listener {
    name: l0
    saslMechanisms: ANONYMOUS
    sslProfile: test-profile
}
listener {
    identity: l1
    saslMechanisms: ANONYMOUS
    port: 1234
}
listener {
    saslMechanisms: ANONYMOUS
    port: 4567
}
"""


class QdrouterTest(unittest.TestCase):
    """Tests for qpid_dispatch_internal.config.qdrouter"""

    def do_test_qdrouter_parse(self, text):
        conf = Config()
        content = conf._parse(text.split("\n"))
        self.maxDiff = None
        expect = [
            ['router', {'mode': 'standalone', 'id': 'QDR'}],
            ['sslProfile', {'password': 'secret', 'name': 'test-profile'}],
            ['listener', {'sslProfile': 'test-profile', 'name': 'l0', 'saslMechanisms': 'ANONYMOUS'}],
            ['listener', {'saslMechanisms': 'ANONYMOUS', 'identity': 'l1', 'port': '1234'}],
            ['listener', {'saslMechanisms': 'ANONYMOUS', 'port': '4567'}]
        ]
        self.assertEqual(content, expect)

        expect = [
            ['router', {'mode': 'standalone', 'id': 'QDR'}],
            ['sslProfile', {'password': 'secret', 'name': 'test-profile'}],
            ['listener', {'name': 'l0', 'sslProfile': 'test-profile', 'saslMechanisms': 'ANONYMOUS'}],
            ['listener', {'port': '1234', 'identity': 'l1', 'saslMechanisms': 'ANONYMOUS'}],
            ['listener', {'port': '4567', 'saslMechanisms': 'ANONYMOUS'}]
        ]
        self.assertEqual(content, expect)

        conf.load(text.split("\n"))
        router = conf.by_type('router')[0]
        listeners = list(conf.by_type('listener'))
        self.assertEqual(len(listeners), 3)

    def test_qdrouter_parse_dash(self):
        self.do_test_qdrouter_parse(conf_text)

    def test_qdrouter_parse_camel(self):
        self.do_test_qdrouter_parse(confText)


if __name__ == '__main__':
    unittest.main()
