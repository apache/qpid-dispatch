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

FIXME aconway 2014-03-27: we need to check what is installed & skip tests that can't be run.

Current we assume the following are installed: 
 - proton with python bindings
 - qpidd with AMQP 1.0 support
 - qpidtoollibs python module from qpid/tools
 - qpid_messaging python module from qpid/cpp

You can set this up from packages on fedora:

  sudo yum install protonc qpid-cpp-server qpid-tools python-qpid-proton python-qpid_messaging

Here's how to build from source assuming you use default install prefix /usr/local

With a qpid checkout at $QPID:
 cd $QPID/qpid/cpp/<build-directory>; make install
 cd $QPID/qpid/tools; ./setup.py install --prefix /usr/local
 cd $QPID/qpid/python; ./setup.py install --prefix /usr/local
With a  qpid-proton checkout at $PROTON
 cd $PROTON/<build-directory>; make install

And finally make sure to set up your environment:

export PATH="$PATH:/usr/local/sbin:/usr/local/bin"
export PYTHONPATH="$PYTHONPATH:/usr/local/lib/proton/bindings/python:/usr/local/lib64/proton/bindings/python:/usr/local/lib/python2.7/site-packages:/usr/local/lib64/python2.7/site-packages"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/usr/local/lib64"
"""

import sys, os, time, socket, random
import subprocess, tempfile, shutil
import unittest
import qpidtoollibs
import qpid_messaging as qm
import proton
from proton import Message, PENDING, ACCEPTED, REJECTED, RELEASED
from copy import copy

def retry_delay(deadline, timeout, delay, max_delay):
    """For internal use in retry. Sleep as required
    and return the new delay or None if retry should time out"""
    remaining = deadline - time.time()
    if remaining <= 0: return None
    time.sleep(min(delay, remaining))
    return min(delay*2, max_delay)


default_timeout=float(os.environ.get("QPID_SYSTEM_TEST_TIMEOUT", 5))

def retry(function, timeout=default_timeout, delay=.001, max_delay=1):
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
            delay = retry_delay(deadline, timeout, delay, max_delay)
            if delay is None: return None

def retry_exception(function, timeout=default_timeout, delay=.001, max_delay=1, exception_test=None):
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
        except Exception, e:
            if exception_test: exception_test(e)
            delay = retry_delay(deadline, timeout, delay, max_delay)
            if delay is None: raise

def port_available(port, host='0.0.0.0'):
    """Return true if connecting to host:port gives 'connection refused'."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.connect((host, port))
        s.close()
    except socket.error, e:
        return e.errno == 111
    except: pass
    return False

def wait_port(port, host='0.0.0.0', **retry_kwargs):
    """Wait up to timeout for port (on host) to be connectable.
    Takes same keyword arguments as retry to control the timeout"""
    def check(e):               # Only retry on connection refused
        if not isinstance(e, socket.error) or not e.errno == 111: raise
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try: retry_exception(lambda: s.connect((host, port)), exception_test=check,
                         **retry_kwargs)
    except Exception, e:
        raise Exception("wait_port timeout on %s:%s: %s"%(host, port, e))

    finally: s.close()

def wait_ports(ports, host="127.0.0.1", **retry_kwargs):
    """Wait up to timeout for all ports (on host) to be connectable.
    Takes same keyword arguments as retry to control the timeout"""
    for p in ports: wait_port(p)

def message(**properties):
    """Convenience to create a proton.Message with properties set"""
    m = Message()
    for name, value in properties.iteritems(): setattr(m, name, value)
    return m

class Process(subprocess.Popen):
    """Popen that can be torn down at the end of a TestCase and stores its output."""

    # Expected states of a Process at teardown
    RUNNING=1                   # Still running
    EXIT_OK=2                   # Exit status 0
    EXIT_FAIL=3                 # Exit status not 0

    def __init__(self, name, args, expect=EXIT_OK, **kwargs):
        self.name, self.args, self.expect = name, args, expect
        self.out = open(name+".out", 'w')
        self.torndown = False
        super(Process, self).__init__(
            args, stdout=self.out, stderr=subprocess.STDOUT, **kwargs)

    def assert_running(self): assert self.poll() is None, "%s exited"%name

    def teardown(self):
        if self.torndown: return
        self.torndown = True
        status = self.poll()
        if status is None: self.kill()
        self.out.close()
        self.check_exit(status)

    def check_exit(self, status):
        def check(condition, expect):
            if status is None: actual="still running"
            else: actual="exit %s"%status
            assert condition, "Expected %s but %s: %s"%(expect, actual, self.name)
        if self.expect == Process.RUNNING: check(status is None, "still running"),
        elif self.expect == Process.EXIT_OK: check(status == 0, "exit 0"),
        elif self.expect == Process.EXIT_FAIL: check(status != 0, "exit non-0")

class Config(object):
    """Base class for configuration objects that provide a convenient
    way to create content for configuration files."""

    def write(self, name, suffix=".conf"):
        """Write the config object to file name.suffix. Returns name.suffix."""
        name = name+suffix
        with open(name,'w') as f: f.write(str(self))
        return name


class Qdrouterd(Process):
    """Run a Qpid Dispatch Router Daemon"""

    class Config(list, Config):
        """List of ('section', {'name':'value',...}).
        Fills in some default values automatically, see Qdrouterd.DEFAULTS
        """

        DEFAULTS = {
            'listener':{'sasl-mechanisms':'ANONYMOUS'},
            'connector':{'sasl-mechanisms':'ANONYMOUS','role':'on-demand'}
        }

        def sections(self, name):
            """Return list of sections named name"""
            return [p for n,p in self if n == name]

        def _defs(self, name, props):
            """Fill in defaults for required values"""
            if not name in Qdrouterd.Config.DEFAULTS: return props
            p = copy(Qdrouterd.Config.DEFAULTS[name])
            p.update(props);
            return p

        def __str__(self):
            """Generate config file content. Fills in defaults for some require values"""
            def props(p): return "".join(["    %s: %s\n"%(k,v) for k,v in p.iteritems()])
            return "".join(["%s {\n%s}\n"%(n,props(self._defs(n,p))) for n,p in self])

    class Agent(object):
        """Management agent"""
        def __init__(self, router):
            self.router = router
            self.messenger = Messenger()
            self.messenger.route("amqp:/*", "amqp://0.0.0.0:%s/$1"%router.ports[0])
            self.address = "amqp:/$management"
            self.subscription = self.messenger.subscribe("amqp:/#")
            self.reply_to = self.subscription.address

        def stop(self): self.messenger.stop()

        def get(self, type):
            """Return a list of attribute dicts for each instance of type"""
            request = message(address=self.address, reply_to=self.reply_to,
                              correlation_id=1,
                              properties={u'operation':u'QUERY', u'entityType':type},
                              body={'attributeNames':[]})
            response = Message()
            self.messenger.put(request)
            self.messenger.send()
            self.messenger.recv(1)
            self.messenger.get(response)
            if response.properties['statusCode'] != 200:
                raise Exception("Agent error: %d %s" % (
                    response.properties['statusCode'],
                    response.properties['statusDescription']))
            attrs = response.body['attributeNames']
            return [dict(zip(attrs, values)) for values in response.body['results']]


    def __init__(self, name, config, **kwargs):
        self.config = copy(config)
        super(Qdrouterd, self).__init__(
            name, ['qdrouterd', '-c', config.write(name)], expect=Process.RUNNING)
        self._agent = None

    @property
    def agent(self):
        if not self._agent: self._agent = self.Agent(self)
        return self._agent

    def teardown(self):
        if self._agent: self._agent.stop()
        super(Qdrouterd, self).teardown()

    @property
    def ports(self):
        """Return list of configured ports for all listeners"""
        return [ l['port'] for l in self.config.sections('listener') ]

    @property
    def addresses(self):
        """Return amqp://host:port addresses for all listeners"""
        return [ "amqp://%s:%s"%(l['addr'],l['port']) for l in self.config.sections('listener') ]

    def is_connected(self, port, host='0.0.0.0'):
        """If router has a connection to host:port return the management info.
        Otherwise return None"""
        connections = self.agent.get('org.apache.qpid.dispatch.connection')
        for c in connections:
            if c['name'] == '%s:%s'%(host, port): return c
        return None


class Qpidd(Process):
    """Run a Qpid Daemon"""

    class Config(dict, Config):

        def __str__(self):
            return "".join(["%s=%s\n"%(k,v) for k,v in self.iteritems()])
        

    def __init__(self, name, config):
        self.config = Qpidd.Config(
            {'auth':'no',
             'log-to-stderr':'false', 'log-to-file':name+".log",
             'data-dir':name+".data"})
        self.config.update(config)
        super(Qpidd, self).__init__(
            name, ['qpidd', '--config', self.config.write(name)], expect=Process.RUNNING)
        self.port = self.config['port'] or 5672
        self.address = "127.0.0.1:%s"%self.port
        self._agent = None

    def qm_connect(self):
        """Make a qpid_messaging connection to the broker"""
        return qm.Connection.establish(self.address)

    @property
    def agent(self, **kwargs):
        if not self._agent: self._agent = qpidtoollibs.BrokerAgent(self.qm_connect())
        return self._agent



class Messenger(proton.Messenger):
    """Minor additions to Messenger for tests"""

    def flush(self):
        """Call work() till there is no work left."""
        while self.work(0.01): pass

    def subscribe(self, source):
        """proton.Messenger.subscribe and work till subscription is visible."""
        t = proton.Messenger.subscribe(self, source)
        self.flush()
        return t

class TestCase(unittest.TestCase):
    """A test case that creates a separate directory for each test and
    cleans up during teardown."""

    def __init__(self, *args, **kwargs):
        super(TestCase, self).__init__(*args, **kwargs)
        self.save_dir = os.getcwd()
        # self.id() is normally _module[.module].TestClass.test_name
        id = self.id().split(".")
        if len(id) == 1:        # Not the expected format, just use dir = id.
            dir = id[0]
        else:                   # use dir = module[.module].TestClass/test_name
            dir = os.path.join(".".join(id[0:-1]), id[-1])
        shutil.rmtree(dir, ignore_errors=True)
        os.makedirs(dir)
        os.chdir(dir)
        self.cleanup_list = []
        self.port_range = (20000, 30000)
        self.next_port = random.randint(*self.port_range)

    def tearDown(self):
        os.chdir(self.save_dir)
        self.cleanup_list.reverse()
        for t in self.cleanup_list:
            for m in ["teardown", "tearDown", "stop", "close"]:
                a = getattr(t, m, None)
                if a: a(); break

    def cleanup(self, x): self.cleanup_list.append(x); return x

    def get_port(self):
        """Get an unused port"""
        def advance():          # Advance with wrap-around
            self.next_port += 1
            if self.next_port >= self.port_range[1]: self.next_port = port_range[0]
        start = self.next_port
        while not port_available(self.next_port):
            advance()
            if self.next_port == start:
                raise Exception("No avaliable ports in range %s", self.port_range)
        p = self.next_port;
        advance()
        return p

    def popen(self, *args, **kwargs):
        """Start a Process that will be cleaned up on teardown"""
        return self.cleanup(Process(*args, **kwargs))

    def qdrouterd(self, *args, **kwargs):
        """Return a Qdrouterd that will be cleaned up on teardown"""
        return self.cleanup(Qdrouterd(*args, **kwargs))

    def qpidd(self, *args, **kwargs):
        """Return a Qpidd that will be cleaned up on teardown"""
        return self.cleanup(Qpidd(*args, **kwargs))

    def messenger(self, name="test-messenger", timeout=default_timeout, blocking=True, cleanup=True):
        """Return a started Messenger that will be cleaned up on teardown."""
        m = Messenger(name)
        m.timeout = timeout
        m.blocking = blocking
        m.start()
        if cleanup: self.cleanup(m)
        return m

    def message(self, **properties):
        """Convenience to create a proton.Message with properties set"""
        global message
        return message(**properties)
