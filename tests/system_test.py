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
with special support for qdrouter processes.

Features:
- Create separate directories for each test.
- Save logs, sub-process output, core files etc.
- Automated clean-up after tests: kill sub-processes etc.
- Tools to manipulate qdrouter configuration files.
- Sundry other tools.
"""

import errno, os, time, socket, random, subprocess, shutil, unittest, __main__, re
from copy import copy
import proton
from proton import Message
from qpid_dispatch.management.client import Node

try:
    # NOTE: the tests can be run outside a build to test an installed dispatch.
    # In this case we won't have access to the run.py module so no valgrind.
    from run import with_valgrind
except ImportError:
    def with_valgrind(args, outfile): return (args, 0)

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

# The directory where this module lives. Used to locate static configuration files etc.
DIR = os.path.dirname(__file__)

def _check_requirements():
    """If requirements are missing, return a message, else return empty string."""
    missing = MISSING_MODULES
    required_exes = ['qdrouterd']
    missing += ["No exectuable %s"%e for e in required_exes if not find_exe(e)]

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

# Valgrind significantly slows down the response time of the router, so use a
# long default timeout
TIMEOUT = float(os.environ.get("QPID_SYSTEM_TEST_TIMEOUT", 60))

def retry(function, timeout=TIMEOUT, delay=.001, max_delay=1):
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

def retry_exception(function, timeout=TIMEOUT, delay=.001, max_delay=1, exception_test=None):
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

def get_local_host_socket(protocol_family='IPv4'):
    if protocol_family == 'IPv4':
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        host = '127.0.0.1'
    elif protocol_family == 'IPv6':
        s = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        host = '::1'

    return s, host

def port_available(port, protocol_family='IPv4'):
    """Return true if connecting to host:port gives 'connection refused'."""
    s, host = get_local_host_socket(protocol_family)

    try:
        s.connect((host, port))
        s.close()
    except socket.error, e:
        return e.errno == errno.ECONNREFUSED
    except:
        pass
    return False

def wait_port(port, protocol_family='IPv4', **retry_kwargs):
    """Wait up to timeout for port (on host) to be connectable.
    Takes same keyword arguments as retry to control the timeout"""
    def check(e):
        """Only retry on connection refused"""
        if not isinstance(e, socket.error) or not e.errno == errno.ECONNREFUSED:
            raise
    s, host = get_local_host_socket(protocol_family)
    try:
        retry_exception(lambda: s.connect((host, port)), exception_test=check,
                        **retry_kwargs)
    except Exception, e:
        raise Exception("wait_port timeout on host %s port %s: %s"%(host, port, e))

    finally: s.close()

def wait_ports(ports, **retry_kwargs):
    """Wait up to timeout for all ports (on host) to be connectable.
    Takes same keyword arguments as retry to control the timeout"""
    for port, protocol_family in ports.iteritems():
        wait_port(port=port, protocol_family=protocol_family, **retry_kwargs)

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
        self.outdir = os.getcwd()
        self.outfile = os.path.abspath(self.unique(self.name))
        self.out = open(self.outfile + '.out', 'w')
        with open(self.outfile + '.cmd', 'w') as f: f.write("%s\n" % ' '.join(args))
        self.torndown = False
        kwargs.setdefault('stdout', self.out)
        kwargs.setdefault('stderr', subprocess.STDOUT)
        args, self.valgrind_error = with_valgrind(args, self.outfile + '.vg')
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
        if status is None:    # still running
            self.terminate()
            rc = self.wait()
            if rc is None:
                self.kill()
            if self.valgrind_error and rc == self.valgrind_error:
                # Report that valgrind found errors
                status = rc;
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
        assert not self.valgrind_error or status != self.valgrind_error, \
            "Valgrind errors (in %s)\n\n%s\n" % (self.outfile+".vg", open(self.outfile+".vg").read())
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
            'listener': {'host':'0.0.0.0', 'saslMechanisms':'ANONYMOUS', 'idleTimeoutSeconds': '120', 'authenticatePeer': 'no'},
            'connector': {'host':'127.0.0.1', 'saslMechanisms':'ANONYMOUS', 'idleTimeoutSeconds': '120', 'role':'on-demand'},
            'router': {'mode': 'standalone', 'id': 'QDR', 'debugDump': 'qddebug.txt'}
        }

        def sections(self, name):
            """Return list of sections named name"""
            return [p for n, p in self if n == name]

        @property
        def router_id(self): return self.sections("router")[0]["id"]

        def defaults(self):
            """Fill in default values in gconfiguration"""
            for name, props in self:
                if name in Qdrouterd.Config.DEFAULTS:
                    for n,p in Qdrouterd.Config.DEFAULTS[name].iteritems():
                        props.setdefault(n,p)

        def __str__(self):
            """Generate config file content. Calls default() first."""
            def props(p):
                return "".join(["    %s: %s\n"%(k, v) for k, v in p.iteritems()])
            self.defaults()
            return "".join(["%s {\n%s}\n"%(n, props(p)) for n, p in self])

    def __init__(self, name=None, config=Config(), pyinclude=None, wait=True):
        """
        @param name: name used for for output files, default to id from config.
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
            self._management = Node.connect(self.addresses[0], timeout=TIMEOUT)
        return self._management

    def teardown(self):
        if self._management:
            try: self._management.close()
            except: pass
        super(Qdrouterd, self).teardown()

    @property
    def ports_family(self):
        """
        Return a dict of listener ports and the respective port family
        Example -
        { 23456: 'IPv4', 243455: 'IPv6' }
        """
        ports_fam = {}
        for l in self.config.sections('listener'):
            if l.get('protocolFamily'):
                ports_fam[l['port']] = l['protocolFamily']
            else:
                ports_fam[l['port']] = 'IPv4'

        return ports_fam

    @property
    def ports(self):
        """Return list of configured ports for all listeners"""
        return [l['port'] for l in self.config.sections('listener')]

    @property
    def addresses(self):
        """Return amqp://host:port addresses for all listeners"""
        address_list = []
        for l in self.config.sections('listener'):
            protocol_family = l.get('protocolFamily')
            if protocol_family == 'IPv6':
                address_list.append("amqp://[%s]:%s"%(l['host'], l['port']))
            elif protocol_family == 'IPv4':
                address_list.append("amqp://%s:%s"%(l['host'], l['port']))
            else:
                # Default to IPv4
                address_list.append("amqp://%s:%s"%(l['host'], l['port']))

        return address_list

    @property
    def hostports(self):
        """Return host:port for all listeners"""
        address_list = []
        for l in self.config.sections('listener'):
            protocol_family = l.get('protocolFamily')
            if protocol_family == 'IPv6':
                address_list.append("[%s]:%s"%(l['host'], l['port']))
            elif protocol_family == 'IPv4':
                address_list.append("%s:%s"%(l['host'], l['port']))
            else:
                # Default to IPv4
                address_list.append("%s:%s"%(l['host'], l['port']))

        return address_list

    def is_connected(self, port, host='127.0.0.1'):
        """If router has a connection to host:port:identity return the management info.
        Otherwise return None"""
        try:
            ret_val = False
            response = self.management.query(type="org.apache.qpid.dispatch.connection")
            index_host = response.attribute_names.index('host')
            for result in response.results:
                outs = '%s:%s' % (host, port)
                if result[index_host] == outs:
                    ret_val = True
            return ret_val
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
                attribute_names=[u'name', u'subscriberCount', u'remoteCount']).get_entities()

            addrs = [a for a in addrs if a['name'].endswith(address)]

            return addrs and addrs[0]['subscriberCount'] >= subscribers and addrs[0]['remoteCount'] >= remotes
        assert retry(check, **retry_kwargs)

    def get_host(self, protocol_family):
        if protocol_family == 'IPv4':
            return '127.0.0.1'
        elif protocol_family == 'IPv6':
            return '::1'
        else:
            return '127.0.0.1'

    def wait_ports(self, **retry_kwargs):
        wait_ports(self.ports_family, **retry_kwargs)

    def wait_connectors(self, **retry_kwargs):
        """
        Wait for all connectors to be connected
        @param retry_kwargs: keyword args for L{retry}
        """
        for c in self.config.sections('connector'):
            assert retry(lambda: self.is_connected(port=c['port'], host=self.get_host(c.get('protocolFamily'))),
                         **retry_kwargs), "Port not connected %s" % c['port']

    def wait_ready(self, **retry_kwargs):
        """Wait for ports and connectors to be ready"""
        if not self._wait_ready:
            self._wait_ready = True
            self.wait_ports(**retry_kwargs)
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

class Messenger(proton.Messenger):
    """Convenience additions to proton.Messenger"""

    def __init__(self, name=None, timeout=TIMEOUT, blocking=True):
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

    # Top level directory above any Tester directories.
    # CMake-generated configuration may be found here.
    top_dir = os.getcwd()

    # The root directory for Tester directories, under top_dir
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
        assert not errors, "Errors during teardown: \n%s" % "\n----".join([str(e) for e in errors])


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
    def get_port(cls, protocol_family='IPv4'):
        """Get an unused port"""
        def advance():
            """Advance with wrap-around"""
            cls.next_port += 1
            if cls.next_port >= cls.port_range[1]:
                cls.next_port = cls.port_range[0]
        start = cls.next_port
        while not port_available(cls.next_port, protocol_family):
            advance()
            if cls.next_port == start:
                raise Exception("No available ports in range %s", cls.port_range)
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
