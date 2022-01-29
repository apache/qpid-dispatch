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

import argparse
import sys
from itertools import combinations

from qpid_dispatch_internal.tools.command import (main,
                                                  UsageError,
                                                  parse_args_qdstat,
                                                  _qdmanage_parser,
                                                  _qdstat_parser)

from system_test import unittest


def mock_error(self, message):
    raise ValueError(message)


argparse.ArgumentParser.error = mock_error  # type: ignore[assignment]  # Cannot assign to a method

# Since BusManager file is defined in tools/qdmanage.in -> tools/qdmanage
# otherwise it could be just imported


class FakeBusManager:
    def displayGeneral(self): pass
    def displayConnections(self): pass
    def displayRouterLinks(self): pass
    def displayRouterNodes(self): pass
    def displayEdges(self): pass
    def displayAddresses(self): pass
    def displayMemory(self): pass
    def displayPolicy(self): pass
    def displayVhosts(self): pass
    def displayVhostgroups(self): pass
    def displayVhoststats(self): pass
    def displayAutolinks(self): pass
    def displayLinkRoutes(self): pass
    def displayLog(self): pass
    def show_all(self): pass


FBM = FakeBusManager


class TestParseArgsQdstat(unittest.TestCase):
    def setUp(self):
        self.parser = _qdstat_parser(BusManager=FBM)

    def test_parse_args_qdstat_print_help(self):
        self.parser.print_help()

    def test_parse_args_qdstat_mutually_exclusive(self):
        options1 = ["-g", "-c",
                    "-l", "-n", "-e", "-a", "-m", "--autolinks", "--linkroutes", "--log",
                    "--all-entities"]
        options2 = ["-r", "--all-routers"]

        def _call_pairs(options):
            for options_pair in combinations(options, 2):
                with self.assertRaises(ValueError):
                    self.parser.parse_args(options_pair)

        _call_pairs(options1)
        _call_pairs(options2)

    def test_parse_args_qdstat_default(self):
        args = parse_args_qdstat(FBM, argv=[])
        self.assertEqual(FBM.displayGeneral.__name__, args.show)

    def test_parse_args_qdstat_method_show_matching(self):
        matching = [("-g", FBM.displayGeneral.__name__),
                    ("-c", FBM.displayConnections.__name__),
                    ("-l", FBM.displayRouterLinks.__name__),
                    ("-n", FBM.displayRouterNodes.__name__),
                    ("-e", FBM.displayEdges.__name__),
                    ("-a", FBM.displayAddresses.__name__),
                    ("-m", FBM.displayMemory.__name__),
                    ("--autolinks", FBM.displayAutolinks.__name__),
                    ("--linkroutes", FBM.displayLinkRoutes.__name__),
                    ("--log", FBM.displayLog.__name__),
                    ("--all-entities", FBM.show_all.__name__),
                    ]
        for option, expected in matching:
            args = self.parser.parse_args([option])
            self.assertEqual(expected, args.show)

    def test_parse_args_qdstat_limit(self):
        args = self.parser.parse_args([])
        self.assertEqual(None, args.limit)

        args = self.parser.parse_args(["--limit", "1"])
        self.assertEqual(1, args.limit)


class TestParseArgsQdmanage(unittest.TestCase):
    def setUp(self):
        self.operations = ["HERE", "SOME", "OPERATIONS"]
        self.parser = _qdmanage_parser(operations=self.operations)

    def test_parse_args_qdmanage_print_help(self):
        self.parser.print_help()

    def test_parse_args_qdmanage_operation_no_args(self):
        argv = "-r r1 QUERY --type some --name the_name -b 127.0.0.1:5672"
        opts, args = self.parser.parse_known_args(argv.split())
        self.assertEqual("QUERY", args[0])

    def test_parse_args_qdmanage_operation_and_args(self):
        argv = "-r r1 QUERY arg1=val1 --type some other=argument --name the_name -b 127.0.0.1:5672"
        opts, args = self.parser.parse_known_args(argv.split())
        self.assertEqual(["QUERY", "arg1=val1", "other=argument"], args)


class TestMain(unittest.TestCase):
    def test_main(self):
        def run_success(argv):
            self.assertEqual(sys.argv, argv)

        def run_raises(argv, _Exception):
            run_success(argv)
            raise _Exception("some")

        def run_raises_UsageError(argv):
            run_raises(argv, UsageError)

        def run_raises_Exception(argv):
            run_raises(argv, Exception)

        def run_raises_KeyboardInterrupt(argv):
            run_raises(argv, KeyboardInterrupt)

        self.assertEqual(0, main(run_success))
        failed_runs = [
            # run_raises_UsageError, ##uncomment this exposes bug
            run_raises_Exception,
            run_raises_KeyboardInterrupt,
        ]
        for run in failed_runs:
            self.assertEqual(1, main(run))


if __name__ == '__main__':
    unittest.main()
