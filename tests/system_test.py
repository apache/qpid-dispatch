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

"""System test library, provides tools for tests that start multiple processes,
with special support for qpidd and qdrouter processes.

Features:
- Create separate directories for each test.
- Save logs, sub-process output, core files etc.
- Automated clean-up after tests: kill sub-processes etc.
- Tools to manipulate qpidd and qdrouter configuration files.
- Sundry other tools.

To run qpidd, additional to basic dispatch requirements:
 - qpidd with AMQP 1.0 support
 - qpidtoollibs python module from qpid/tools
 - qpid_messaging python module from qpid/cpp

You can set this up from packages on fedora:

  sudo yum install protonc qpid-cpp-server qpid-tools python-qpid-proton python-qpid_messaging

Here's how to build from source assuming you use default install prefix /usr/local

With a  qpid-proton checkout at $PROTON
 cd $PROTON/<build-directory>; make install
With a qpid checkout at $QPID:
 cd $QPID/qpid/cpp/<build-directory>; make install
 cd $QPID/qpid/tools; ./setup.py install --prefix /usr/local
 cd $QPID/qpid/python; ./setup.py install --prefix /usr/local

And finally make sure to set up your environment:

export PATH="$PATH:/usr/local/sbin:/usr/local/bin"
export PYTHONPATH="$PYTHONPATH:/usr/local/lib/proton/bindings/python:/usr/local/lib64/proton/bindings/python:/usr/local/lib/python2.7/site-packages:/usr/local/lib64/python2.7/site-packages"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/usr/local/lib64"
"""

import os, time, socket, random, subprocess, shutil, unittest, __main__, re
from copy import copy
import proton
from proton import Message
from qpid_dispatch.management.client import Node
try:
    # NOTE: the tests can be run outside a build to test an installed dispatch.
    # In this case we won't have access to the run.py module so no valgrind.
    from run import with_valgrind
except ImportError:
    def with_valgrind(args, outfile): return args

# Optional modules
MISSING_MODULES = []

try:
    import qpidtoollibs
except ImportError, err:
    qpidtoollibs = None         # pylint: disable=invalid-name
    MISSING_MODULES.append(str(err))

try:
    import qpid_messaging as qm
except ImportError, err:
    qm = None                   # pylint: disable=invalid-name
    MISSING_MODULES.append(str(err))

def find_exe(program):
    """Find an executable in the system PATH"""
    def is_exe(fpath):
        """True if fpath is executable"""
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)
    mydir = os.path.split(program)[0]
    if mydir:
        if is_exe(program):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file
    return None


def _check_requirements():
    """If requirements are missing, return a message, else return empty string."""
    missing = MISSING_MODULES
    required_exes = ['qpidd', 'qdrouterd']
    missing += ["No exectuable %s"%e for e in required_exes if not find_exe(e)]
    if find_exe('qpidd'):
        p = subprocess.Popen(['qpidd', '--help'], stdout=subprocess.PIPE)
        if not "AMQP 1.0" in p.communicate()[0]:
            missing.append("No AMQP 1.0 support in qpidd")
    if missing:
        return "%s: %s"%(__name__, ", ".join(missing))

MISSING_REQUIREMENTS = _check_requirements()

def retry_delay(deadline, delay, max_delay):
    """For internal use in retry. Sleep as required
    and return the new delay or None if retry should time out"""
    remaining = deadline - time.time()
    if remaining <= 0:
        return None
    time.sleep(min(delay, remaining))
    return min(delay*2, max_delay)


DEFAULT_TIMEOUT = float(os.environ.get("QPID_SYSTEM_TEST_TIMEOUT", 10))

def retry(function, timeout=DEFAULT_TIMEOUT, delay=.001, max_delay=1):
    """Call function until it returns a true value or timeout expires.
    Double the delay for each retry up to max_delay.
    Returns what function returns or None if timeout expires.
    """
    deadline = time.time() + timeout
    while True:
        ret = function()
        if ret:
            return ret
        else:
            delay = retry_delay(deadline, delay, max_delay)
            if delay is None:
                return None

def retry_exception(function, timeout=DEFAULT_TIMEOUT, delay=.001, max_delay=1, exception_test=None):
    """Call function until it returns without exception or timeout expires.
    Double the delay for each retry up to max_delay.
    Calls exception_test with any exception raised by function, exception_test
    may itself raise an exception to terminate the retry.
    Returns what function returns if it succeeds before timeout.
    Raises last exception raised by function on timeout.
    """
    deadline = time.time() + timeout
    while True:
        try:
            return function()
        except Exception, e:    # pylint: disable=broad-except
            if exception_test:
                exception_test(e)
            delay = retry_delay(deadline, delay, max_delay)
            if delay is None:
                raise

def port_available(port, host='0.0.0.0'):
    """Return true if connecting to host:port gives 'connection refused'."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.connect((host, port))
        s.close()
    except socket.error, e:
        return e.errno == 111
    except:
        pass
    return False

def wait_port(port, host='0.0.0.0', **retry_kwargs):
    """Wait up to timeout for port (on host) to be connectable.
    Takes same keyword arguments as retry to control the timeout"""
    def check(e):
        """Only retry on connection refused"""
        if not isinstance(e, socket.error) or not e.errno == 111:
            raise
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        retry_exception(lambda: s.connect((host, port)), exception_test=check,
                        **retry_kwargs)
    except Exception, e:
        raise Exception("wait_port timeout on host %s port %s: %s"%(host, port, e))

    finally: s.close()

def wait_ports(ports, host="127.0.0.1", **retry_kwargs):
    """Wait up to timeout for all ports (on host) to be connectable.
    Takes same keyword arguments as retry to control the timeout"""
    for p in ports:
        wait_port(p, host=host, **retry_kwargs)

def message(**properties):
    """Convenience to create a proton.Message with properties set"""
    m = Message()
    for name, value in properties.iteritems():
        getattr(m, name)        # Raise exception if not a valid message attribute.
        setattr(m, name, value)
    return m

class Process(subprocess.Popen):
    """
    Popen that can be torn down at the end of a TestCase and stores its output.
    Uses valgrind if enabled.
    """

    # Expected states of a Process at teardown
    RUNNING = 1                   # Still running
    EXIT_OK = 2                   # Exit status 0
    EXIT_FAIL = 3                 # Exit status not 0

    unique_id = 0
    @classmethod
    def unique(cls, name):
        cls.unique_id += 1
        return "%s-%s" % (name, cls.unique_id)

    def __init__(self, args, name=None, expect=EXIT_OK, **kwargs):
        """
        Takes same arguments as subprocess.Popen. Some additional/special args:
        @param expect: Raise error if process staus not as expected at end of test:
            L{RUNNING} - expect still running.
            L{EXIT_OK} - expect proces to have terminated with 0 exit status.
            L{EXIT_ERROR} - expect proces to have terminated with non-0 exit status.
        @keyword stdout: Defaults to the file name+".out"
        @keyword stderr: Defaults to be the same as stdout
        """
        self.name = name or os.path.basename(args[0])
        self.args, self.expect = args, expect
        self.outfile = self.unique(self.name)
        self.out = open(self.outfile + '.out', 'w')
        with open(self.outfile + '.cmd', 'w') as f: f.write("%s\n" % ' '.join(args))
        self.torndown = False
        kwargs.setdefault('stdout', self.out)
        kwargs.setdefault('stderr', subprocess.STDOUT)
        args = with_valgrind(args, self.outfile + '.vg')
        try:
            super(Process, self).__init__(args, **kwargs)
        except Exception, e:
            raise Exception("subprocess.Popen(%s, %s) failed: %s: %s" %
                            (args, kwargs, type(e).__name__, e))

    def assert_running(self):
        """Assert that the proces is still running"""
        assert self.poll() is None, "%s exited" % self.name

    def teardown(self):
        """Check process status and stop the process if necessary"""
        if self.torndown:
            return
        self.torndown = True
        status = self.poll()
        if status is None:
            self.terminate()
            if self.wait() is None:
                self.kill()
        self.out.close()
        self.check_exit(status)

    def check_exit(self, status):
        """Check process exit status"""
        def check(condition, expect):
            """assert condition with a suitable message for status"""
            if status is None:
                actual = "still running"
            else:
                actual = "exit %s"%status
            assert condition, "Expected %s but %s: %s"%(expect, actual, self.name)
        if self.expect == Process.RUNNING:
            check(status is None, "still running")
        elif self.expect == Process.EXIT_OK:
            check(status == 0, "exit 0")
        elif self.expect == Process.EXIT_FAIL:
            check(status != 0, "exit non-0")

class Config(object):
    """Base class for configuration objects that provide a convenient
    way to create content for configuration files."""

    def write(self, name, suffix=".conf"):
        """Write the config object to file name.suffix. Returns name.suffix."""
        name = name+suffix
        with open(name, 'w') as f:
            f.write(str(self))
        return name

class Qdrouterd(Process):
    """Run a Qpid Dispatch Router Daemon"""

    class Config(list, Config):
        """
        List of ('section', {'name':'value', ...}).

        Fills in some default values automatically, see Qdrouterd.DEFAULTS
        """

        DEFAULTS = {
            'listener':{'addr':'0.0.0.0', 'sasl-mechanisms':'ANONYMOUS'},
            'connector':{'addr':'0.0.0.0', 'sasl-mechanisms':'ANONYMOUS', 'role':'on-demand'},
            'container':{'debugDump':"qddebug.txt"}
        }

        def sections(self, name):
            """Return list of sections named name"""
            return [p for n, p in self if n == name]

        @property
        def router_id(self): return self.sections("router")[0]["routerId"]

        def defaults(self):
            """Fill in default values in gconfiguration"""
            for name, props in self:
                if name in Qdrouterd.Config.DEFAULTS:
                    for n,p in Qdrouterd.Config.DEFAULTS[name].iteritems():
                        props.setdefault(n,p)

        def __str__(self):
            """Generate config file content. Calls default() first."""
            def props(p):
                """qpidd.conf format of dict p"""
                return "".join(["    %s: %s\n"%(k, v) for k, v in p.iteritems()])
            self.defaults()
            return "".join(["%s {\n%s}\n"%(n, props(p)) for n, p in self])

    def __init__(self, name=None, config=Config(), pyinclude=None, wait=True):
        """
        @param name: name used for for output files, default to routerId from config.
        @param config: router configuration
        @keyword wait: wait for router to be ready (call self.wait_ready())
        """
        self.config = copy(config)
        if not name: name = self.config.router_id
        assert name
        default_log = [l for l in config if (l[0] == 'log' and l[1]['module'] == 'DEFAULT')]
        if not default_log:
            config.append(
                ('log', {'module':'DEFAULT', 'enable':'trace+', 'source': 'true', 'output':name+'.log'}))
        args = ['qdrouterd', '-c', config.write(name)]
        env_home = os.environ.get('QPID_DISPATCH_HOME')
        if pyinclude:
            args += ['-I', pyinclude]
        elif env_home:
            args += ['-I', os.path.join(env_home, 'python')]
        super(Qdrouterd, self).__init__(args, name=name, expect=Process.RUNNING)
        self._management = None
        self._wait_ready = False
        if wait:
            self.wait_ready()


    @property
    def management(self):
        """Return a management agent proxy for this router"""
        if not self._management:
            self._management = Node.connect(self.addresses[0], timeout=DEFAULT_TIMEOUT)
        return self._management

    def teardown(self):
        if self._management:
            try: self._management.close()
            except: pass
        super(Qdrouterd, self).teardown()

    @property
    def ports(self):
        """Return list of configured ports for all listeners"""
        return [l['port'] for l in self.config.sections('listener')]

    @property
    def addresses(self):
        """Return amqp://host:port addresses for all listeners"""
        return ["amqp://anonymous@%s:%s"%(l['addr'], l['port']) for l in self.config.sections('listener')]

    @property
    def hostports(self):
        """Return host:port for all listeners"""
        return ["%s:%s"%(l['addr'], l['port']) for l in self.config.sections('listener')]

    def is_connected(self, port, host='0.0.0.0'):
        """If router has a connection to host:port return the management info.
        Otherwise return None"""
        try:
            return self.management.read(identity="connection/%s:%s" % (host, port))
        except:
            return False

    def wait_address(self, address, subscribers=0, remotes=0, **retry_kwargs):
        """
        Wait for an address to be visible on the router.
        @keyword subscribers: Wait till subscriberCount >= subscribers
        @keyword remotes: Wait till remoteCount >= remotes
        @param retry_kwargs: keyword args for L{retry}
        """
        def check():
            # TODO aconway 2014-06-12: this should be a request by name, not a query.
            # Need to rationalize addresses in management attributes.
            # endswith check is because of M0/L/R prefixes
            addrs = self.management.query(
                type='org.apache.qpid.dispatch.router.address',
                attribute_names=['name', 'subscriberCount', 'remoteCount']).get_entities()
            addrs = [a for a in addrs if a['name'].endswith(address)]
            return addrs and addrs[0]['subscriberCount'] >= subscribers and addrs[0]['remoteCount'] >= remotes
        assert retry(check, **retry_kwargs)

    def wait_connectors(self, **retry_kwargs):
        """
        Wait for all connectors to be connected
        @param retry_kwargs: keyword args for L{retry}
        """
        for c in self.config.sections('connector'):
            assert retry(lambda: self.is_connected(c['port']), **retry_kwargs), "Port not connected %s" % c['port']

    def wait_ready(self, **retry_kwargs):
        """Wait for ports and connectors to be ready"""
        if not self._wait_ready:
            self._wait_ready = True
            wait_ports(self.ports, **retry_kwargs)
            self.wait_connectors(**retry_kwargs)
        return self

    def is_router_connected(self, router_id, **retry_kwargs):
        try:
            self.management.read(identity="router.node/%s" % router_id)
            # TODO aconway 2015-01-29: The above check should be enough, we
            # should not advertise a remote router in managment till it is fully
            # connected. However we still get a race where the router is not
            # actually ready for traffic. Investigate.
            # Meantime the following actually tests send-thru to the router.
            node = Node.connect(self.addresses[0], router_id, timeout=1)
            return retry_exception(lambda: node.query('org.apache.qpid.dispatch.router'))
        except:
            return False


    def wait_router_connected(self, router_id, **retry_kwargs):
        retry(lambda: self.is_router_connected(router_id), **retry_kwargs)

class Qpidd(Process):
    """Run a Qpid Daemon"""

    class Config(dict, Config):
        """qpidd.conf contents. Use like  a dict, str() generates qpidd.conf format"""
        def __str__(self):
            return "".join(["%s=%s\n"%(k, v) for k, v in self.iteritems()])

    def __init__(self, name=None, config=Config(), port=None, wait=True):
        self.config = Qpidd.Config(
            {'auth':'no',
             'log-to-stderr':'false', 'log-to-file':name+".log",
             'data-dir':name+".data"})
        self.config.update(config)
        if port:
            self.config['port'] = port
        super(Qpidd, self).__init__(
            ['qpidd', '--config', self.config.write(name)],
            name=name, expect=Process.RUNNING)
        self.port = self.config['port'] or 5672
        self.address = "127.0.0.1:%s"%self.port
        self._management = None
        if wait:
            self.wait_ready()

    def qm_connect(self):
        """Make a qpid_messaging connection to the broker"""
        if not qm:
            raise Exception("No qpid_messaging module available")
        return qm.Connection.establish(self.address)

    @property
    def management(self, **kwargs):
        """Get the management agent proxy for this broker"""
        if not qpidtoollibs:
            raise Exception("No qpidtoollibs module available")
        if not self._management:
            self._management = qpidtoollibs.BrokerAgent(self.qm_connect(), **kwargs)
        return self._management

    def wait_ready(self):
        wait_port(self.port)

class Messenger(proton.Messenger):
    """Convenience additions to proton.Messenger"""

    def __init__(self, name=None, timeout=DEFAULT_TIMEOUT, blocking=True):
        super(Messenger, self).__init__(name)
        self.timeout = timeout
        self.blocking = blocking

    def flush(self):
        """Call work() till there is no work left."""
        while self.work(0.1):
            pass

    def fetch(self, accept=True):
        """Fetch a single message"""
        msg = Message()
        self.recv(1)
        self.get(msg)
        if accept:
            self.accept()
        return msg

    def subscribe(self, source, **retry_args):
        """Do a proton.Messenger.subscribe and wait till the address is available."""
        subscription = super(Messenger, self).subscribe(source)
        assert retry(lambda: subscription.address, **retry_args) # Wait for address
        return subscription

class Tester(object):
    """Tools for use by TestCase
- Create a directory for the test.
- Utilities to create processes and servers, manage ports etc.
- Clean up processes on teardown"""

    # Wipe the old test tree when we are first imported.
    root_dir = os.path.abspath(__name__+'.dir')

    def __init__(self, id):
        """
        @param id: module.class.method or False if no directory should be created
        """
        self.directory = os.path.join(self.root_dir, *id.split('.'))
        self.cleanup_list = []

    def rmtree(self):
        """Remove old test class results directory"""
        shutil.rmtree(os.path.dirname(self.directory), ignore_errors=True)

    def setup(self):
        """Called from test setup and class setup."""
        os.makedirs(self.directory)
        os.chdir(self.directory)

    def teardown(self):
        """Clean up (tear-down, stop or close) objects recorded via cleanup()"""
        self.cleanup_list.reverse()
        errors = []
        for obj in self.cleanup_list:
            try:
                for method in ["teardown", "tearDown", "stop", "close"]:
                    cleanup = getattr(obj, method, None)
                    if cleanup:
                        cleanup()
                        break
            except Exception, e:
                errors.append(e)
        assert not errors, "Errors during teardown: %s" % errors


    def cleanup(self, x):
        """Record object x for clean-up during tear-down.
        x should have on of the methods teardown, tearDown, stop or close"""
        self.cleanup_list.append(x)
        return x

    def popen(self, *args, **kwargs):
        """Start a Process that will be cleaned up on teardown"""
        return self.cleanup(Process(*args, **kwargs))

    def qdrouterd(self, *args, **kwargs):
        """Return a Qdrouterd that will be cleaned up on teardown"""
        return self.cleanup(Qdrouterd(*args, **kwargs))

    def qpidd(self, *args, **kwargs):
        """Return a Qpidd that will be cleaned up on teardown"""
        return self.cleanup(Qpidd(*args, **kwargs))

    def messenger(self, name=None, cleanup=True, **kwargs):
        """Return a started Messenger that will be cleaned up on teardown."""
        m = Messenger(name or os.path.basename(self.directory), **kwargs)
        m.start()
        if cleanup:
            self.cleanup(m)
        return m

    port_range = (20000, 30000)
    next_port = random.randint(port_range[0], port_range[1])

    @classmethod
    def get_port(cls):
        """Get an unused port"""
        def advance():
            """Advance with wrap-around"""
            cls.next_port += 1
            if cls.next_port >= cls.port_range[1]:
                cls.next_port = cls.port_range[0]
        start = cls.next_port
        while not port_available(cls.next_port):
            advance()
            if cls.next_port == start:
                raise Exception("No avaliable ports in range %s", cls.port_range)
        p = cls.next_port
        advance()
        return p


class TestCase(unittest.TestCase, Tester): # pylint: disable=too-many-public-methods
    """A TestCase that sets up its own working directory and is also a Tester."""

    def __init__(self, test_method):
        unittest.TestCase.__init__(self, test_method)
        Tester.__init__(self, self.id())

    @classmethod
    def setUpClass(cls):
        cls.tester = Tester('.'.join([cls.__module__, cls.__name__, 'setUpClass']))
        cls.tester.rmtree()
        cls.tester.setup()

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, 'tester'):
            cls.tester.teardown()
            del cls.tester

    def setUp(self):
        # Python < 2.7 will call setUp on the system_test.TestCase class
        # itself as well as the subclasses. Ignore that.
        if self.__class__ is TestCase: return
        # Hack to support setUpClass on older python.
        # If the class has not already been set up, do it now.
        if not hasattr(self.__class__, 'tester'):
            try:
                self.setUpClass()
            except:
                if hasattr(self.__class__, 'tester'):
                    self.__class__.tester.teardown()
                raise
        Tester.setup(self)

    def tearDown(self):
        # Python < 2.7 will call tearDown on the system_test.TestCase class
        # itself as well as the subclasses. Ignore that.
        if self.__class__ is TestCase: return
        Tester.teardown(self)
        # Hack to support tearDownClass on older versions of python.
        if hasattr(self.__class__, '_tear_down_class'):
            self.tearDownClass()

    def skipTest(self, reason):
        """Workaround missing unittest.TestCase.skipTest in python 2.6.
        The caller must return in order to end the test"""
        if hasattr(unittest.TestCase, 'skipTest'):
            unittest.TestCase.skipTest(self, reason)
        else:
            print "Skipping test", self.id(), reason

    # Hack to support tearDownClass on older versions of python.
    # The default TestLoader sorts tests alphabetically so we insert
    # a fake tests that will run last to call tearDownClass.
    # NOTE: definitely not safe for a parallel test-runner.
    if not hasattr(unittest.TestCase, 'tearDownClass'):
        def test_zzzz_teardown_class(self):
            """Fake test to call tearDownClass"""
            if self.__class__ is not TestCase:
                self.__class__._tear_down_class = True

    def assert_fair(self, seq):
        avg = sum(seq)/len(seq)
        for i in seq:
            assert i > avg/2, "Work not fairly distributed: %s"%seq

    def assertIn(self, item, items):
        assert item in items, "%s not in %s" % (item, items)

    if not hasattr(unittest.TestCase, 'assertRegexpMatches'):
        def assertRegexpMatches(self, text, regexp, msg=None):
            """For python < 2.7: assert re.search(regexp, text)"""
            assert re.search(regexp, text), msg or "Can't find %r in '%s'" %(regexp, text)

def main_module():
    """
    Return the module name of the __main__ module - i.e. the filename with the
    path and .py extension stripped. Useful to run the tests in the current file but
    using the proper module prefix instead of '__main__', as follows:
        if __name__ == '__main__':
            unittest.main(module=main_module())
    """
    return os.path.splitext(os.path.basename(__main__.__file__))[0]
