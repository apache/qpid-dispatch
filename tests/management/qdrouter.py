##
## Licensed to the Apache Software Foundation (ASF) under one
## or more contributor license agreements.  See the NOTICE file
## distributed with this work for additional information
## regarding copyright ownership.  The ASF licenses this file
## to you under the Apache License, Version 2.0 (the
## "License"); you may not use this file except in compliance
## with the License.  You may obtain a copy of the License at
##
##   http://www.apache.org/licenses/LICENSE-2.0
##
## Unless required by applicable law or agreed to in writing,
## software distributed under the License is distributed on an
## "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
## KIND, either express or implied.  See the License for the
## specific language governing permissions and limitations
## under the License
##

#pylint: disable=wildcard-import,unused-wildcard-import,missing-docstring,too-many-public-methods

import unittest
from qpid_dispatch_internal.management.qdrouter import *

class QdrouterTest(unittest.TestCase):
    """Tests for qpid_dispatch_internal.config.qdrouter"""

    def test_qdrouter_parse(self):
        conf = Configuration()
        conf_text = """
# Line comment
router {
    mode: standalone            # End of line comment
}
ssl-profile {
    name: test-profile
    password: secret
}
listener {
    name: l0
    sasl-mechanisms: ANONYMOUS
    ssl-profile: test-profile
}
listener {
    identity: l1
    sasl-mechanisms: ANONYMOUS
    port: 1234
}
listener {
    sasl-mechanisms: ANONYMOUS
    port: 4567
}
        """
        #pylint: disable=protected-access
        content = conf._parse(conf_text.split("\n"))

        self.maxDiff = None     # pylint: disable=invalid-name
        self.assertEqual(content, [
            ["router", {"mode":"standalone"}],
            ["ssl-profile", {"name":"test-profile", "password":"secret"}],
            ["listener", {"name":"l0", "sasl-mechanisms":"ANONYMOUS", "ssl-profile":"test-profile"}],
            ["listener", {"identity":"l1", "sasl-mechanisms":"ANONYMOUS", "port":"1234"}],
            ["listener", {"sasl-mechanisms":"ANONYMOUS", "port":"4567"}]
        ])

        content = conf._expand(content)
        self.assertEqual(content, [
            ["router", {"mode":"standalone"}],
            ["listener", {"name":"l0", "sasl-mechanisms":"ANONYMOUS", "password":"secret"}],
            ["listener", {"identity":"l1", "sasl-mechanisms":"ANONYMOUS", "port":"1234"}],
            ["listener", {"sasl-mechanisms":"ANONYMOUS", "port":"4567"}]
        ])

        content = conf._default_ids(content)
        self.assertEqual(content, [
            ["router", {"mode":"standalone", "name":"router0", "identity":"router0"}],
            ["listener", {"name":"l0", "identity":"l0", "sasl-mechanisms":"ANONYMOUS", "password":"secret"}],
            ["listener", {"name":"l1", "identity":"l1", "sasl-mechanisms":"ANONYMOUS", "port":"1234"}],
            ["listener", {"name":"listener2", "identity":"listener2", "sasl-mechanisms":"ANONYMOUS", "port":"4567"}]
        ])

        conf.load(conf_text.split("\n"))
        self.assertEqual(conf.router.name, 'router0')
        self.assertEqual(conf.router.identity, 'router0')
        self.assertEqual(len(conf.listener), 3)
        self.assertEqual(conf.listener[0].name, 'l0')
        self.assertEqual(conf.listener[2].name, 'listener2')
        self.assertEqual(conf.listener[2].identity, 'listener2')

if __name__ == '__main__':
    unittest.main()
