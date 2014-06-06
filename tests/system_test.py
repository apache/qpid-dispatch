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

Requires the following:
 - proton with python bindings
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

import os, time, socket, random, subprocess, shutil, unittest, inspect
from copy import copy
import proton
from proton import Message

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
    missing += ["No exectuable  %s"%e for e in required_exes if not find_exe(e)]
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


DEFAULT_TIMEOUT = float(os.environ.get("QPID_SYSTEM_TEST_TIMEOUT", 5))

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
        raise Exception("wait_port timeout on %s:%s: %s"%(host, port, e))

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
        setattr(m, name, value)
    return m

class Process(subprocess.Popen):
    """Popen that can be torn down at the end of a TestCase and stores its output."""

    # Expected states of a Process at teardown
    RUNNING = 1                   # Still running
    EXIT_OK = 2                   # Exit status 0
    EXIT_FAIL = 3                 # Exit status not 0

    def __init__(self, name, args, expect=EXIT_OK, **kwargs):
        self.name, self.args, self.expect = name, args, expect
        self.out = open(name+".out", 'w')
        self.torndown = False
        super(Process, self).__init__(
            args, stdout=self.out, stderr=subprocess.STDOUT, **kwargs)

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

    # def __getitem(self, key):
    #     """Get an item, make sure any defaults have been set first"""
    #     # defaults()
    #     return super(Config, self).__getitem__(self, key)

class Qdrouterd(Process):
    """Run a Qpid Dispatch Router Daemon"""

    class Config(list, Config):
        """List of ('section', {'name':'value', ...}).
        Fills in some default values automatically, see Qdrouterd.DEFAULTS
        """

        DEFAULTS = {
            'listener':{'addr':'0.0.0.0', 'sasl-mechanisms':'ANONYMOUS'},
            'connector':{'addr':'0.0.0.0', 'sasl-mechanisms':'ANONYMOUS', 'role':'on-demand'}
        }

        def sections(self, name):
            """Return list of sections named name"""
            return [p for n, p in self if n == name]

        def defaults(self):
            """Fill in default values in configuration"""
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

    class Agent(object):
        """Management agent"""
        def __init__(self, router):
            self.router = router
            self.messenger = Messenger()
            self.messenger.route("amqp:/*", "amqp://0.0.0.0:%s/$1"%router.ports[0])
            self.address = "amqp:/$management"
            self.subscription = self.messenger.subscribe("amqp:/#")
            self.reply_to = self.subscription.address

        def stop(self):
            """Stop the agent's messenger"""
            self.messenger.stop()

        def get(self, entity_type):
            """Return a list of attribute dicts for each instance of entity_type"""
            request = message(
                address=self.address, reply_to=self.reply_to,
                correlation_id=1,
                properties={u'operation':u'QUERY', u'entityType':entity_type},
                body={'attributeNames':[]})
            self.messenger.put(request)
            response = self.messenger.fetch()
            if response.properties['statusCode'] != 200:
                raise Exception("Agent error: %d %s" % (
                    response.properties['statusCode'],
                    response.properties['statusDescription']))
            attrs = response.body['attributeNames']
            return [dict(zip(attrs, values)) for values in response.body['results']]


    def __init__(self, name, config=Config()):
        self.config = copy(config)
        super(Qdrouterd, self).__init__(
            name, ['qdrouterd', '-c', config.write(name)], expect=Process.RUNNING)
        self._agent = None

    @property
    def agent(self):
        """Return an management Agent for this router"""
        if not self._agent:
            self._agent = self.Agent(self)
        return self._agent

    def teardown(self):
        if self._agent:
            self._agent.stop()
        super(Qdrouterd, self).teardown()

    @property
    def ports(self):
        """Return list of configured ports for all listeners"""
        return [l['port'] for l in self.config.sections('listener')]

    @property
    def addresses(self):
        """Return amqp://host:port addresses for all listeners"""
        return ["amqp://%s:%s"%(l['addr'], l['port']) for l in self.config.sections('listener')]

    def is_connected(self, port, host='0.0.0.0'):
        """If router has a connection to host:port return the management info.
        Otherwise return None"""
        connections = self.agent.get('org.apache.qpid.dispatch.connection')
        for c in connections:
            if c['name'] == '%s:%s'%(host, port):
                return c
        return None

    def wait_connectors(self):
        """Wait for all connectors to be connected"""
        for c in self.config.sections('connector'):
            retry(lambda: self.is_connected(c['port']))

    def wait_ready(self):
        """Wait for ports and connectors to be ready"""
        wait_ports(self.ports)
        self.wait_connectors()

class Qpidd(Process):
    """Run a Qpid Daemon"""

    class Config(dict, Config):
        """qpidd.conf contents. Use like  a dict, str() generates qpidd.conf format"""
        def __str__(self):
            return "".join(["%s=%s\n"%(k, v) for k, v in self.iteritems()])

    def __init__(self, name, config=Config(), port=None):
        self.config = Qpidd.Config(
            {'auth':'no',
             'log-to-stderr':'false', 'log-to-file':name+".log",
             'data-dir':name+".data"})
        self.config.update(config)
        if port:
            self.config['port'] = port
        super(Qpidd, self).__init__(
            name, ['qpidd', '--config', self.config.write(name)], expect=Process.RUNNING)
        self.port = self.config['port'] or 5672
        self.address = "127.0.0.1:%s"%self.port
        self._agent = None

    def qm_connect(self):
        """Make a qpid_messaging connection to the broker"""
        if not qm:
            raise Exception("No qpid_messaging module available")
        return qm.Connection.establish(self.address)

    @property
    def agent(self, **kwargs):
        """Get the management agent for this broker"""
        if not qpidtoollibs:
            raise Exception("No qpidtoollibs module available")
        if not self._agent:
            self._agent = qpidtoollibs.BrokerAgent(self.qm_connect(), **kwargs)
        return self._agent



# Decorator to add an optional flush argument to a method, defaulting to
# the _flush value for the messenger.
def flush_arg(method):
    """Decorator for Messenger methods that adds an optional flush argument,
    defaulting to the Messenger default"""
    def wrapper(self, *args, **kwargs):
        """Wrapper that adds flush argument"""
        flush = self._flush # pylint: disable=protected-access
        if 'flush' in kwargs:
            flush = kwargs['flush']
            del kwargs['flush']
        r = method(self, *args, **kwargs)
        if flush:
            self.flush()
        return r
    return wrapper

class Messenger(proton.Messenger): # pylint: disable=too-many-public-methods
    """Convenience additions to proton.Messenger"""

    def __init__(self, name=None, timeout=DEFAULT_TIMEOUT, blocking=True, flush=False):
        super(Messenger, self).__init__(name)
        self.timeout = timeout
        self.blocking = blocking
        self._flush = flush

    def flush(self):
        """Call work() till there is no work left."""
        while self.work(0.1):
            pass

    @flush_arg
    def fetch(self, accept=True):
        """Fetch a single message"""
        msg = Message()
        self.recv(1)
        self.get(msg)
        if accept:
            self.accept()
        return msg

    put = flush_arg(proton.Messenger.put)
    subscribe = flush_arg(proton.Messenger.subscribe)

class Tester(object):
    """Tools for use by TestCase
- Create a directory for the test.
- Utilities to create processes and servers, manage ports etc.
- Clean up processes on teardown"""

    def __init__(self, *args, **kwargs):
        self.cleanup_list = []
        self.save_dir = None
        self.directory = None

    def setup(self, directory):
        """Create directory"""
        self.directory = directory
        shutil.rmtree(directory, ignore_errors=True)
        os.makedirs(directory)
        self.save_dir = os.getcwd()
        os.chdir(directory)

    def teardown(self):
        """Clean up (tear-down, stop or close) objects recorded via cleanup()"""
        self.cleanup_list.reverse()
        for t in self.cleanup_list:
            for m in ["teardown", "tearDown", "stop", "close"]:
                a = getattr(t, m, None)
                if a:
                    a()
                    break
        os.chdir(self.save_dir)

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

    _base_dir = None

    def __init__(self, test_method):
        unittest.TestCase.__init__(self, test_method)
        Tester.__init__(self)

    @classmethod
    def base_dir(cls):
        if not cls._base_dir:
            cls._base_dir = os.path.abspath(os.path.join(__name__+'.dir', cls.__name__))
        return cls._base_dir

    @classmethod
    def setUpClass(cls):
        # Don't delete cwd out from under ourselves.
        # cwd can be a subdir of base_dir if we were called by test_0000_setup_class
        if os.path.commonprefix([os.getcwd(), cls.base_dir()]) == cls.base_dir():
            os.chdir(os.path.dirname(cls.base_dir()))
        shutil.rmtree(cls.base_dir(), ignore_errors=True) # Clear old test tree.
        cls.tester = Tester()
        cls.tester.setup(os.path.join(cls.base_dir(), 'setup_class'))

    @classmethod
    def tearDownClass(cls):
        if inspect.isclass(cls):
            cls.tester.teardown()

    def setUp(self):
        # self.id() is normally the fully qualified method name
        Tester.setup(self, os.path.join(self.base_dir(), self.id().split(".")[-1]))

    def tearDown(self):
        Tester.teardown(self)

    def skipTest(self, reason):
        """Workaround missing unittest.TestCase.skipTest in python 2.6.
        The caller must return in order to end the test"""
        if hasattr(unittest.TestCase, 'skipTest'):
            self.skipTest(reason)
        else:
            print "Skipping test", id(), reason

    # Hack to support setUpClass/tearDownClass on older versions of python.
    # The default TestLoader sorts tests alphabetically so we insert
    # fake tests that will run first and last to call the class setup/teardown functions.
    if not hasattr(unittest.TestCase, 'setUpClass'):
        def test_0000_setup_class(self):
            """Fake test to call setUpClass"""
            self.setUpClass()
        def test_zzzz_teardown_class(self):
            """Fake test to call tearDownClass"""
            self.tearDownClass()

    def assert_fair(self, seq):
        avg = sum(seq)/len(seq)
        for i in seq:
            assert i > avg/2, "Work not fairly distributed: %s"%seq
