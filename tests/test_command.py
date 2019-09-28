import unittest, sys

from qpid_dispatch_internal.tools.command import main, UsageError

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
            #run_raises_UsageError, ##uncomment this exposes bug
            run_raises_Exception,
            run_raises_KeyboardInterrupt,
        ]
        for run in failed_runs:
            self.assertEqual(1, main(run))

if __name__ == '__main__':
    unittest.main()

