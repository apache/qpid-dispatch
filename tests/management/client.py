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

"""Tests for management.amqp"""

import unittest
from qpid_dispatch_internal.management import Url

class UrlTest(unittest.TestCase):

    def test_url(self):
        url = Url(scheme='amqp', user='me', password='secret', host='myhost', port=1234, path='foobar')
        self.assertEqual(str(url), "amqp://me:secret@myhost:1234/foobar")
        self.assertEqual(
            [url.scheme, url.user, url.password, url.host, url.port, url.path],
            ['amqp', 'me', 'secret', 'myhost', 1234, 'foobar']
        )

        self.assertEqual(str(url), "amqp://me:secret@myhost:1234/foobar")
        self.assertNotEqual(str(url), "amqps://me:secret@myhost:1234/foobar")
        self.assertNotEqual(str(url), "me:secret@myhost:1234/foobar")
        self.assertNotEqual(str(url), "amqp://notme:secret@myhost:1234/foobar")
        self.assertNotEqual(str(url), "amqp://me:notsecret@myhost:1234/foobar")
        self.assertNotEqual(str(url), "amqp://me:secret@notmyhost:1234/foobar")
        self.assertNotEqual(str(url), "amqp://me:secret@myhost:1234/notfoobar")
        self.assertNotEqual(str(url), "amqp://me:secret@myhost:5555/foobar")

        # Check that we allow None for scheme, port
        url = Url(user='me', password='secret', host='myhost', path='foobar')
        self.assertEqual(str(url), "me:secret@myhost/foobar")
        self.assertEqual(
            [url.scheme, url.user, url.password, url.host, url.port, url.path],
            [None, 'me', 'secret', 'myhost', None, 'foobar']
        )

        # Scheme defaults
        self.assertEqual(str(Url("me:secret@myhost/foobar").defaults()),
                         "amqp://me:secret@myhost:5672/foobar")
        # Correct port for amqps vs. amqps
        self.assertEqual(str(Url("amqps://me:secret@myhost/foobar").defaults()),
                         "amqps://me:secret@myhost:5671/foobar")
        self.assertEqual(str(Url("amqp://me:secret@myhost/foobar").defaults()),
                         "amqp://me:secret@myhost:5672/foobar")

        # Empty string for path
        self.assertEqual(Url("myhost/").path, "")
        self.assertIsNone(Url("myhost").path)
