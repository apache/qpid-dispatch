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

import errno
import fcntl
import json
import logging
import os
import pathlib
import queue as Queue
import random
import re
import shlex
import shutil
import socket
import subprocess
import sys
import time
import unittest
import uuid
from copy import copy
from datetime import datetime
from subprocess import PIPE, STDOUT
from threading import Event
from threading import Thread
from typing import Callable, TextIO, List, Optional, Tuple

import __main__

import proton
import proton.utils
from proton import Delivery
from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import AtLeastOnce, Container
from proton.reactor import AtMostOnce
from qpid_dispatch.management.client import Node
from qpid_dispatch.management.error import NotFoundStatus

# Optional modules
MISSING_MODULES = []

try:
    import qpidtoollibs
except ImportError as err:
    qpidtoollibs = None         # pylint: disable=invalid-name
    MISSING_MODULES.append(str(err))

try:
    import qpid_messaging as qm
except ImportError as err:
    qm = None  # pylint: disable=invalid-name
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
    missing += ["No exectuable %s" % e for e in required_exes if not find_exe(e)]

    if missing:
        return "%s: %s" % (__name__, ", ".join(missing))


MISSING_REQUIREMENTS = _check_requirements()


def retry_delay(deadline, delay, max_delay):
    """For internal use in retry. Sleep as required
    and return the new delay or None if retry should time out"""
    remaining = deadline - time.time()
    if remaining <= 0:
        return None
    time.sleep(min(delay, remaining))
    return min(delay * 2, max_delay)


# Valgrind significantly slows down the response time of the router, so use a
# long default timeout
TIMEOUT = float(os.environ.get("QPID_SYSTEM_TEST_TIMEOUT", 60))


def retry(function: Callable[[], bool], timeout: float = TIMEOUT, delay: float = .001, max_delay: float = 1):
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
        except Exception as e:  # pylint: disable=broad-except
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


def port_refuses_connection(port, protocol_family='IPv4'):
    """Return true if connecting to host:port gives 'connection refused'."""
    s, host = get_local_host_socket(protocol_family)
    try:
        s.connect((host, port))
    except OSError as e:
        return e.errno == errno.ECONNREFUSED
    finally:
        s.close()

    return False


def port_permits_binding(port, protocol_family='IPv4'):
    """Return true if binding to the port succeeds."""
    s, _ = get_local_host_socket(protocol_family)
    host = ""
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # so that followup binders are not blocked
        s.bind((host, port))
    except OSError:
        return False
    finally:
        s.close()

    return True


def port_available(port, protocol_family='IPv4'):
    """Return true if a new server will be able to bind to the port."""
    return port_refuses_connection(port, protocol_family) and port_permits_binding(port, protocol_family)


def wait_port(port, protocol_family='IPv4', **retry_kwargs):
    """Wait up to timeout for port (on host) to be connectable.
    Takes same keyword arguments as retry to control the timeout"""
    def check(e):
        """Only retry on connection refused"""
        if not isinstance(e, socket.error) or not e.errno == errno.ECONNREFUSED:
            raise

    host = None

    def connect():
        # macOS gives EINVAL for all connection attempts after a ECONNREFUSED
        # man 3 connect: "If connect() fails, the state of the socket is unspecified. [...]"
        s, host = get_local_host_socket(protocol_family)
        try:
            s.connect((host, port))
        finally:
            s.close()

    try:
        retry_exception(connect, exception_test=check, **retry_kwargs)
    except Exception as e:
        raise Exception("wait_port timeout on host %s port %s: %s" % (host, port, e))


def wait_ports(ports, **retry_kwargs):
    """Wait up to timeout for all ports (on host) to be connectable.
    Takes same keyword arguments as retry to control the timeout"""
    for port, protocol_family in ports.items():
        wait_port(port=port, protocol_family=protocol_family, **retry_kwargs)


def message(**properties):
    """Convenience to create a proton.Message with properties set"""
    m = Message()
    for name, value in properties.items():
        getattr(m, name)        # Raise exception if not a valid message attribute.
        setattr(m, name, value)
    return m


def skip_test_in_ci(environment_var):
    env_var = os.environ.get(environment_var)
    if env_var is not None:
        if env_var.lower() in ['true', '1', 't', 'y', 'yes']:
            return True
    return False


class Process(subprocess.Popen):
    """
    Popen that can be torn down at the end of a TestCase and stores its output.
    """

    # Expected states of a Process at teardown
    RUNNING = -1                # Still running
    EXIT_OK = 0                 # Exit status 0
    EXIT_FAIL = 1               # Exit status 1

    unique_id = 0

    @classmethod
    def unique(cls, name):
        cls.unique_id += 1
        return "%s-%s" % (name, cls.unique_id)

    def __init__(self, args, name=None, expect=EXIT_OK, **kwargs):
        """
        Takes same arguments as subprocess.Popen. Some additional/special args:
        @param expect: Raise error if process status not as expected at end of test:
            L{RUNNING} - expect still running.
            L{EXIT_OK} - expect process to have terminated with 0 exit status.
            L{EXIT_FAIL} - expect process to have terminated with exit status 1.
            integer    - expected return code
        @keyword stdout: Defaults to the file name+".out"
        @keyword stderr: Defaults to be the same as stdout
        """
        self.name = name or os.path.basename(args[0])
        self.args = args
        self.expect = expect
        self.outdir = os.getcwd()
        self.outfile = os.path.abspath(self.unique(self.name))
        self.torndown = False
        with open(self.outfile + '.out', 'w') as out:
            kwargs.setdefault('stdout', out)
            kwargs.setdefault('stderr', subprocess.STDOUT)
            try:
                super(Process, self).__init__(args, **kwargs)
                with open(self.outfile + '.cmd', 'w') as f:
                    f.write("%s\npid=%s\n" % (' '.join(args), self.pid))
            except Exception as e:
                raise Exception("subprocess.Popen(%s, %s) failed: %s: %s" %
                                (args, kwargs, type(e).__name__, e))

    def assert_running(self):
        """Assert that the process is still running"""
        assert self.poll() is None, "%s: exited" % ' '.join(self.args)

    def teardown(self):
        """Check process status and stop the process if necessary"""
        if self.torndown:
            return
        self.torndown = True

        def error(msg):
            with open(self.outfile + '.out') as f:
                raise RuntimeError("Process %s error: %s\n%s\n%s\n>>>>\n%s<<<<" % (
                    self.pid, msg, ' '.join(self.args),
                    self.outfile + '.cmd', f.read()))

        status = self.poll()
        if status is None:      # Still running
            self.terminate()
            if self.expect is not None and self.expect != Process.RUNNING:
                error("still running")
            self.expect = 0     # Expect clean exit after terminate
            status = self.wait()
        if self.expect is not None and self.expect != status:
            error("exit code %s, expected %s" % (status, self.expect))


class Config:
    """Base class for configuration objects that provide a convenient
    way to create content for configuration files."""

    def write(self, name, suffix=".conf"):
        """Write the config object to file name.suffix. Returns name.suffix."""
        name = name + suffix
        with open(name, 'w') as f:
            f.write(str(self))
        return name


class Qdrouterd(Process):
    """Run a Qpid Dispatch Router Daemon"""

    class Config(list, Config):  # type: ignore[misc]  # Cannot resolve name "Config" (possible cyclic definition)  # mypy#10958
        """
        A router configuration.

        The Config class is a list of tuples in the following format:

        [ ('section-name', {attribute-map}), ...]

        where attribute-map is a dictionary of key+value pairs.  Key is an
        attribute name (string), value can be any of [scalar | string | dict]

        When written to a configuration file to be loaded by the router:
        o) there is no ":' between the section-name and the opening brace
        o) attribute keys are separated by a ":" from their values
        o) attribute values that are scalar or string follow the ":" on the
        same line.
        o) attribute values do not have trailing commas
        o) The section-name and attribute keywords are written
        without enclosing quotes
        o) string type attribute values are not enclosed in quotes
        o) attribute values of type dict are written in their JSON representation.

        Fills in some default values automatically, see Qdrouterd.DEFAULTS
        """

        DEFAULTS = {
            'listener': {'host': '0.0.0.0', 'saslMechanisms': 'ANONYMOUS', 'idleTimeoutSeconds': '120',
                         'authenticatePeer': 'no', 'role': 'normal'},
            'connector': {'host': '127.0.0.1', 'saslMechanisms': 'ANONYMOUS', 'idleTimeoutSeconds': '120'},
            'router': {'mode': 'standalone', 'id': 'QDR'}
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
                    for n, p in Qdrouterd.Config.DEFAULTS[name].items():
                        props.setdefault(n, p)

        def __str__(self):
            """Generate config file content. Calls default() first."""
            def tabs(level):
                if level:
                    return "    " * level
                return ""

            def value(item, level):
                if isinstance(item, dict):
                    result = "{\n"
                    result += "".join(["%s%s: %s,\n" % (tabs(level + 1),
                                                        json.dumps(k),
                                                        json.dumps(v))
                                       for k, v in item.items()])
                    result += "%s}" % tabs(level)
                    return result
                return "%s" %  item

            def attributes(e, level):
                assert(isinstance(e, dict))
                # k = attribute name
                # v = string | scalar | dict
                return "".join(["%s%s: %s\n" % (tabs(level),
                                                k,
                                                value(v, level + 1))
                                for k, v in e.items()])

            self.defaults()
            # top level list of tuples ('section-name', dict)
            return "".join(["%s {\n%s}\n" % (n, attributes(p, 1)) for n, p in self])

    def __init__(self, name=None, config=Config(), pyinclude=None, wait=True,
                 perform_teardown=True, cl_args=None, expect=Process.RUNNING):
        """
        @param name: name used for for output files, default to id from config.
        @param config: router configuration
        @keyword wait: wait for router to be ready (call self.wait_ready())
        """
        cl_args = cl_args or []
        self.config = copy(config)
        self.perform_teardown = perform_teardown
        if not name:
            name = self.config.router_id
        assert name
        # setup log and debug dump files
        self.dumpfile = os.path.abspath('%s-qddebug.txt' % name)
        self.config.sections('router')[0]['debugDumpFile'] = self.dumpfile
        default_log = [l for l in config if (l[0] == 'log' and l[1]['module'] == 'DEFAULT')]
        if not default_log:
            self.logfile = "%s.log" % name
            config.append(
                ('log', {'module': 'DEFAULT', 'enable': 'trace+',
                         'includeSource': 'true', 'outputFile': self.logfile}))
        else:
            self.logfile = default_log[0][1].get('outputFile')
        args = ['qdrouterd', '-c', config.write(name)] + cl_args
        env_home = os.environ.get('QPID_DISPATCH_HOME')
        if pyinclude:
            args += ['-I', pyinclude]
        elif env_home:
            args += ['-I', os.path.join(env_home, 'python')]

        # shlex.split parses -ex 'thread apply all' into two parts, not in 4 words as string split does
        args = shlex.split(os.environ.get('QPID_DISPATCH_RUNNER', '')) + args
        super(Qdrouterd, self).__init__(args, name=name, expect=expect)
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
            try:
                self._management.close()
            except:
                pass
            self._management = None

        if not self.perform_teardown:
            return

        teardown_exc = None
        try:
            super(Qdrouterd, self).teardown()
        except Exception as exc:
            # re-raise _after_ dumping all the state we can
            teardown_exc = exc

        def check_output_file(filename, description):
            """check router's debug dump file for anything interesting (should be
            empty) and dump it to stderr for perusal by organic lifeforms"""
            try:
                if os.stat(filename).st_size > 0:
                    with open(filename) as f:
                        sys.stderr.write("\nRouter %s %s:\n>>>>\n" %
                                         (self.config.router_id, description))
                        sys.stderr.write(f.read())
                        sys.stderr.write("\n<<<<\n")
                        sys.stderr.flush()
            except OSError:
                # failed to open file.  This can happen when an individual test
                # spawns a temporary router (i.e. not created as part of the
                # TestCase setUpClass method) that gets cleaned up by the test.
                pass

        check_output_file(filename=self.outfile + '.out', description="output file")
        check_output_file(filename=self.dumpfile, description="debug dump file")

        if teardown_exc:
            # teardown failed - possible router crash?
            # dump extra stuff (command line, output, log)

            def tail_file(fname, line_count=50):
                """Tail a file to a list"""
                out = []
                with open(fname) as f:
                    line = f.readline()
                    while line:
                        out.append(line)
                        if len(out) > line_count:
                            out.pop(0)
                        line = f.readline()
                return out

            try:
                for fname in [("output", self.outfile + '.out'),
                              ("command", self.outfile + '.cmd')]:
                    with open(fname[1]) as f:
                        sys.stderr.write("\nRouter %s %s file:\n>>>>\n" %
                                         (self.config.router_id, fname[0]))
                        sys.stderr.write(f.read())
                        sys.stderr.write("\n<<<<\n")

                if self.logfile:
                    sys.stderr.write("\nRouter %s log file tail:\n>>>>\n" %
                                     self.config.router_id)
                    tail = tail_file(os.path.join(self.outdir, self.logfile))
                    for ln in tail:
                        sys.stderr.write("%s" % ln)
                    sys.stderr.write("\n<<<<\n")
                sys.stderr.flush()
            except OSError:
                # ignore file not found in case test never opens these
                pass

            raise teardown_exc

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

    def _cfg_2_host_port(self, c):
        host = c['host']
        port = c['port']
        protocol_family = c.get('protocolFamily', 'IPv4')
        if protocol_family == 'IPv6':
            return "[%s]:%s" % (host, port)
        elif protocol_family == 'IPv4':
            return "%s:%s" % (host, port)
        raise Exception("Unknown protocol family: %s" % protocol_family)

    @property
    def addresses(self):
        """Return amqp://host:port addresses for all listeners"""
        cfg = self.config.sections('listener')
        return ["amqp://%s" % self._cfg_2_host_port(l) for l in cfg]

    @property
    def connector_addresses(self):
        """Return list of amqp://host:port for all connectors"""
        cfg = self.config.sections('connector')
        return ["amqp://%s" % self._cfg_2_host_port(c) for c in cfg]

    @property
    def hostports(self):
        """Return host:port for all listeners"""
        return [self._cfg_2_host_port(l) for l in self.config.sections('listener')]

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

    def wait_address(self, address, subscribers=0, remotes=0, containers=0,
                     count=1, **retry_kwargs):
        """
        Wait for an address to be visible on the router.
        @keyword subscribers: Wait till subscriberCount >= subscribers
        @keyword remotes: Wait till remoteCount >= remotes
        @keyword containers: Wait till containerCount >= remotes
        @keyword count: Wait until >= count matching addresses are found
        @param retry_kwargs: keyword args for L{retry}
        """
        def check():
            # TODO aconway 2014-06-12: this should be a request by name, not a query.
            # Need to rationalize addresses in management attributes.
            # endswith check is because of M0/L/R prefixes
            addrs = self.management.query(
                type='org.apache.qpid.dispatch.router.address',
                attribute_names=['name', 'subscriberCount', 'remoteCount', 'containerCount']).get_entities()

            addrs = [a for a in addrs if a['name'].endswith(address)]

            return (len(addrs) >= count
                    and addrs[0]['subscriberCount'] >= subscribers
                    and addrs[0]['remoteCount'] >= remotes
                    and addrs[0]['containerCount'] >= containers)
        assert retry(check, **retry_kwargs)

    def wait_address_unsubscribed(self, address, **retry_kwargs):
        """
        Block until address has no subscribers
        """
        a_type = 'org.apache.qpid.dispatch.router.address'

        def check():
            addrs = self.management.query(a_type).get_dicts()
            rc = [a for a in addrs if a['name'].endswith(address)]
            count = 0
            for a in rc:
                count += a['subscriberCount']
                count += a['remoteCount']
                count += a['containerCount']

            return count == 0
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

    def wait_startup_message(self, **retry_kwargs):
        """Wait for router startup message to be printed into logfile

        This ensures that the router installs its signal handlers, avoiding
        a router failure with return code -15 upon premature SIGTERM (DISPATCH-1689)

        e.g. 2022-03-03 19:08:13.608655 +0100 SERVER (notice) Operational, 4 Threads Running (process ID 2190110)
        """
        def _is_startup_line_present(f: TextIO) -> bool:
            for line in f:
                m = re.search(r'SERVER \(notice\) Operational, (\d+) Threads Running \(process ID (\d+)\)', line)
                if m:
                    return True
            return False

        logfile_path = self.logfile_path
        # system_tests_log_level_update filters SERVER module logs to a separate file
        server_log = [l for l in self.config if (l[0] == 'log' and l[1]['module'] == 'SERVER')]
        if server_log:
            logfile_path = os.path.join(self.outdir, server_log[0][1].get('outputFile'))

        assert retry(lambda: pathlib.Path(logfile_path).is_file(), **retry_kwargs), \
            f"Router logfile {logfile_path} does not exist or is not a file"
        with open(logfile_path, 'rt') as router_log:
            assert retry(lambda: _is_startup_line_present(router_log), **retry_kwargs),\
                "Router startup line not present in router log"

    def wait_ready(self, **retry_kwargs):
        """Wait for ports and connectors to be ready"""
        if not self._wait_ready:
            self._wait_ready = True
            self.wait_ports(**retry_kwargs)
            self.wait_connectors(**retry_kwargs)
            self.wait_startup_message(**retry_kwargs)
        return self

    def is_router_connected(self, router_id, **retry_kwargs):
        node = None
        try:
            self.management.read(identity="router.node/%s" % router_id)
            # TODO aconway 2015-01-29: The above check should be enough, we
            # should not advertise a remote router in management till it is fully
            # connected. However we still get a race where the router is not
            # actually ready for traffic. Investigate.
            # Meantime the following actually tests send-thru to the router.
            node = Node.connect(self.addresses[0], router_id, timeout=1)
            return retry_exception(lambda: node.query('org.apache.qpid.dispatch.router'))
        except (proton.ConnectionException, NotFoundStatus, proton.utils.LinkDetached):
            # proton.ConnectionException: the router is not yet accepting connections
            # NotFoundStatus: the queried router is not yet connected
            # TODO(DISPATCH-2119) proton.utils.LinkDetached: should be removed, currently needed for DISPATCH-2033
            return False
        finally:
            if node:
                node.close()

    def wait_router_connected(self, router_id, **retry_kwargs):
        retry(lambda: self.is_router_connected(router_id), **retry_kwargs)

    @property
    def logfile_path(self):
        """Path to a DEFAULT logfile"""
        return os.path.join(self.outdir, self.logfile)


class Tester:
    """Tools for use by TestCase
- Create a directory for the test.
- Utilities to create processes and servers, manage ports etc.
- Clean up processes on teardown"""

    # Top level directory above any Tester directories.
    # CMake-generated configuration may be found here.
    top_dir = os.getcwd()

    # The root directory for Tester directories, under top_dir
    root_dir = os.path.abspath(__name__ + '.dir')

    # Minimum and maximum port number for free port searches
    port_range = (20000, 30000)

    def __init__(self, id):
        """
        @param id: module.class.method or False if no directory should be created
        """
        self.directory = os.path.join(self.root_dir, *id.split('.')) if id else None
        self.cleanup_list = []

        self.port_file = pathlib.Path(self.top_dir, "next_port.lock").open("a+t")
        self.cleanup(self.port_file)

    def rmtree(self):
        """Remove old test class results directory"""
        if self.directory:
            shutil.rmtree(os.path.dirname(self.directory), ignore_errors=True)

    def setup(self):
        """Called from test setup and class setup."""
        if self.directory:
            os.makedirs(self.directory)
            os.chdir(self.directory)

    def _next_port(self) -> int:
        """Reads and increments value stored in self.port_file, under an exclusive file lock.

        When a lock cannot be acquired immediately, fcntl.lockf blocks.

        Failure possibilities:
            File locks may not work correctly on network filesystems. We still should be no worse off than we were.

            This method always unlocks the lock file, so it should not ever deadlock other tests running in parallel.
            Even if that happened, the lock is unlocked by the OS when the file is closed, which happens automatically
            when the process that opened and locked it ends.

            Invalid content in the self.port_file will break this method. Manual intervention is then required.
        """
        try:
            fcntl.flock(self.port_file, fcntl.LOCK_EX)

            # read old value
            self.port_file.seek(0, os.SEEK_END)
            if self.port_file.tell() != 0:
                self.port_file.seek(0)
                port = int(self.port_file.read())
            else:
                # file is empty
                port = random.randint(self.port_range[0], self.port_range[1])

            next_port = port + 1
            if next_port >= self.port_range[1]:
                next_port = self.port_range[0]

            # write new value
            self.port_file.seek(0)
            self.port_file.truncate(0)
            self.port_file.write(str(next_port))
            self.port_file.flush()

            return port
        finally:
            fcntl.flock(self.port_file, fcntl.LOCK_UN)

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
            except Exception as exc:
                errors.append(exc)
        if errors:
            raise RuntimeError("Errors during teardown: \n\n%s" % "\n\n".join([str(e) for e in errors]))

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

    port_range = (20000, 30000)
    next_port = random.randint(port_range[0], port_range[1])

    def get_port(self, protocol_family='IPv4'):
        """Get an unused port"""
        p = self._next_port()
        start = p
        while not port_available(p, protocol_family):
            p = self._next_port()
            if p == start:
                raise Exception("No available ports in range %s", self.port_range)
        return p


class TestCase(unittest.TestCase, Tester):  # pylint: disable=too-many-public-methods
    """A TestCase that sets up its own working directory and is also a Tester."""

    tester: Tester

    def __init__(self, test_method):
        unittest.TestCase.__init__(self, test_method)
        Tester.__init__(self, self.id())

    @classmethod
    def setUpClass(cls):
        cls.maxDiff = None
        cls.tester = Tester('.'.join([cls.__module__, cls.__name__, 'setUpClass']))
        cls.tester.rmtree()
        cls.tester.setup()

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, 'tester'):
            cls.tester.teardown()
            del cls.tester

    def setUp(self):
        Tester.setup(self)

    def tearDown(self):
        Tester.teardown(self)

    def assert_fair(self, seq):
        avg = sum(seq) / len(seq)
        for i in seq:
            assert i > avg / 2, "Work not fairly distributed: %s" % seq

    if not hasattr(unittest.TestCase, 'assertRegex'):
        def assertRegex(self, text, regexp, msg=None):
            assert re.search(regexp, text), msg or "Can't find %r in '%s'" % (regexp, text)

    if not hasattr(unittest.TestCase, 'assertNotRegex'):
        def assertNotRegex(self, text, regexp, msg=None):
            assert not re.search(regexp, text), msg or "Found %r in '%s'" % (regexp, text)


def main_module():
    """
    Return the module name of the __main__ module - i.e. the filename with the
    path and .py extension stripped. Useful to run the tests in the current file but
    using the proper module prefix instead of '__main__', as follows:
        if __name__ == '__main__':
            unittest.main(module=main_module())
    """
    return os.path.splitext(os.path.basename(__main__.__file__))[0]


class AsyncTestReceiver(MessagingHandler):
    """
    A simple receiver that runs in the background and queues any received
    messages.  Messages can be retrieved from this thread via the queue member.
    :param wait: block the constructor until the link has been fully
                 established.
    :param recover_link: restart on remote link detach
    """
    Empty = Queue.Empty

    class MyQueue(Queue.Queue):
        def __init__(self, receiver):
            self._async_receiver = receiver
            super(AsyncTestReceiver.MyQueue, self).__init__()

        def get(self, timeout=TIMEOUT):
            self._async_receiver.num_queue_gets += 1
            msg = super(AsyncTestReceiver.MyQueue, self).get(timeout=timeout)
            self._async_receiver._logger.log("message %d get"
                                             % self._async_receiver.num_queue_gets)
            return msg

        def put(self, msg):
            self._async_receiver.num_queue_puts += 1
            super(AsyncTestReceiver.MyQueue, self).put(msg)
            self._async_receiver._logger.log("message %d put"
                                             % self._async_receiver.num_queue_puts)

    def __init__(self, address, source, conn_args=None, container_id=None,
                 wait=True, recover_link=False, msg_args=None, print_to_console=False):
        if msg_args is None:
            msg_args = {}
        super(AsyncTestReceiver, self).__init__(**msg_args)
        self.address = address
        self.source = source
        self.conn_args = conn_args
        self.queue = AsyncTestReceiver.MyQueue(self)
        self._conn = None
        self._container = Container(self)
        cid = container_id or "ATR-%s:%s" % (source, uuid.uuid4())
        self._container.container_id = cid
        self._ready = Event()
        self._recover_link = recover_link
        self._recover_count = 0
        self._stop_thread = False
        self._thread = Thread(target=self._main)
        self._logger = Logger(title="AsyncTestReceiver %s" % cid, print_to_console=print_to_console)
        self._thread.daemon = True
        self._thread.start()
        self.num_queue_puts = 0
        self.num_queue_gets = 0
        if wait and self._ready.wait(timeout=TIMEOUT) is False:
            raise Exception("Timed out waiting for receiver start")
        self.queue_stats = "self.num_queue_puts=%d, self.num_queue_gets=%d"

    def get_queue_stats(self):
        return self.queue_stats % (self.num_queue_puts, self.num_queue_gets)

    def _main(self):
        self._container.timeout = 0.5
        self._container.start()
        self._logger.log("AsyncTestReceiver Starting reactor")
        while self._container.process():
            if self._stop_thread:
                if self._conn:
                    self._conn.close()
                    self._conn = None
        self._logger.log("AsyncTestReceiver reactor thread done")

    def on_connection_error(self, event):
        self._logger.log("AsyncTestReceiver on_connection_error=%s" % event.connection.remote_condition.description)

    def on_link_error(self, event):
        self._logger.log("AsyncTestReceiver on_link_error=%s" % event.link.remote_condition.description)

    def stop(self, timeout=TIMEOUT):
        self._stop_thread = True
        self._container.wakeup()
        self._thread.join(timeout=TIMEOUT)
        self._logger.log("thread done")
        if self._thread.is_alive():
            raise Exception("AsyncTestReceiver did not exit")
        del self._conn
        del self._container

    def on_start(self, event):
        kwargs = {'url': self.address}
        if self.conn_args:
            kwargs.update(self.conn_args)
        self._conn = event.container.connect(**kwargs)

    def on_connection_opened(self, event):
        self._logger.log("Connection opened")
        kwargs = {'source': self.source}
        event.container.create_receiver(event.connection, **kwargs)

    def on_link_opened(self, event):
        self._logger.log("link opened")
        self._ready.set()

    def on_link_closing(self, event):
        self._logger.log("link closing")
        event.link.close()
        if self._recover_link and not self._stop_thread:
            # lesson learned: the generated link name will be the same as the
            # old link (which is bad) so we specify a new one
            self._recover_count += 1
            kwargs = {'source': self.source,
                      'name': "%s:%s" % (event.link.name, self._recover_count)}
            rcv = event.container.create_receiver(event.connection,
                                                  **kwargs)

    def on_message(self, event):
        self.queue.put(event.message)

    def on_disconnected(self, event):
        # if remote terminates the connection kill the thread else it will spin
        # on the cpu
        self._logger.log("Disconnected")
        if self._conn:
            self._conn.close()
            self._conn = None

    def dump_log(self):
        self._logger.dump()


class AsyncTestSender(MessagingHandler):
    """
    A simple sender that runs in the background and sends 'count' messages to a
    given target.
    """
    class TestSenderException(Exception):
        def __init__(self, error=None):
            super(AsyncTestSender.TestSenderException, self).__init__(error)

    def __init__(self, address, target, count=1, message=None,
                 container_id=None, presettle=False, print_to_console=False):
        super(AsyncTestSender, self).__init__(auto_accept=False,
                                              auto_settle=False)
        self.address = address
        self.target = target
        self.total = count
        self.presettle = presettle
        self.accepted = 0
        self.released = 0
        self.modified = 0
        self.rejected = 0
        self.sent = 0
        self.error = None
        self.link_stats = None
        self._conn = None
        self._sender = None
        self._message = message or Message(body="test")
        self._container = Container(self)
        cid = container_id or "ATS-%s:%s" % (target, uuid.uuid4())
        self._container.container_id = cid
        self._link_name = "%s-%s" % (cid, "tx")
        self._thread = Thread(target=self._main)
        self._thread.daemon = True
        self._logger = Logger(title="AsyncTestSender %s" % cid, print_to_console=print_to_console)
        self._thread.start()
        self.msg_stats = "self.sent=%d, self.accepted=%d, self.released=%d, self.modified=%d, self.rejected=%d"

    def _main(self):
        self._container.timeout = 0.5
        self._container.start()
        self._logger.log("AsyncTestSender Starting reactor")
        while self._container.process():
            self._check_if_done()
        self._logger.log("AsyncTestSender reactor thread done")

    def get_msg_stats(self):
        return self.msg_stats % (self.sent, self.accepted, self.released, self.modified, self.rejected)

    def wait(self):
        # don't stop it - wait until everything is sent
        self._logger.log("AsyncTestSender wait: about to join thread")
        self._thread.join(timeout=TIMEOUT)
        self._logger.log("AsyncTestSender wait: thread done")
        assert not self._thread.is_alive(), "sender did not complete"
        if self.error:
            raise AsyncTestSender.TestSenderException(self.error)
        del self._sender
        del self._conn
        del self._container
        self._logger.log("AsyncTestSender wait: no errors in wait")

    def on_start(self, event):
        self._conn = self._container.connect(self.address)

    def on_connection_opened(self, event):
        self._logger.log("Connection opened")
        option = AtMostOnce if self.presettle else AtLeastOnce
        self._sender = self._container.create_sender(self._conn,
                                                     target=self.target,
                                                     options=option(),
                                                     name=self._link_name)

    def on_sendable(self, event):
        if self.sent < self.total:
            self._sender.send(self._message)
            self.sent += 1
            self._logger.log("message %d sent" % self.sent)

    def _check_if_done(self):
        done = (self.sent == self.total
                and (self.presettle
                     or (self.accepted + self.released + self.modified
                         + self.rejected == self.sent)))
        if done and self._conn:
            self.link_stats = get_link_info(self._link_name,
                                            self.address)
            self._conn.close()
            self._conn = None
            self._logger.log("Connection closed")

    def on_accepted(self, event):
        self.accepted += 1
        event.delivery.settle()
        self._logger.log("message %d accepted" % self.accepted)

    def on_released(self, event):
        # for some reason Proton 'helpfully' calls on_released even though the
        # delivery state is actually MODIFIED
        if event.delivery.remote_state == Delivery.MODIFIED:
            return self.on_modified(event)
        self.released += 1
        event.delivery.settle()
        self._logger.log("message %d released" % self.released)

    def on_modified(self, event):
        self.modified += 1
        event.delivery.settle()
        self._logger.log("message %d modified" % self.modified)

    def on_rejected(self, event):
        self.rejected += 1
        event.delivery.settle()
        self._logger.log("message %d rejected" % self.rejected)

    def on_link_error(self, event):
        self.error = "link error:%s" % str(event.link.remote_condition)
        self._logger.log(self.error)
        if self._conn:
            self._conn.close()
            self._conn = None

    def on_disconnected(self, event):
        # if remote terminates the connection kill the thread else it will spin
        # on the cpu
        self.error = "connection to remote dropped"
        self._logger.log(self.error)
        if self._conn:
            self._conn.close()
            self._conn = None

    def dump_log(self):
        self._logger.dump()


class QdManager:
    """
    A means to invoke qdmanage during a testcase
    """

    def __init__(self, address: Optional[str] = None,
                 timeout: Optional[float] = TIMEOUT,
                 router_id: Optional[str] = None,
                 edge_router_id: Optional[str] = None) -> None:
        # 'tester' - can be 'self' when called in a test,
        # or an instance any class derived from Process (like Qdrouterd)
        self._timeout = timeout
        self._address = address
        self.router_id = router_id
        self.edge_router_id = edge_router_id
        self.router: List[str] = []
        if self.router_id:
            self.router = self.router + ['--router', self.router_id]
        elif self.edge_router_id:
            self.router = self.router + ['--edge-router', self.edge_router_id]

    def __call__(self, cmd: str,
                 address: Optional[str] = None,
                 input: Optional[str] = None,
                 timeout: Optional[float] = None) -> str:
        addr = address or self._address
        assert addr, "address missing"
        with subprocess.Popen(['qdmanage'] + cmd.split(' ') + self.router
                              + ['--bus', addr, '--indent=-1', '--timeout',
                                 str(timeout or self._timeout)], stdin=PIPE,
                              stdout=PIPE, stderr=STDOUT,
                              universal_newlines=True) as p:
            rc = p.communicate(input)
            if p.returncode != 0:
                raise Exception("%s %s" % rc)
            return rc[0]

    def create(self, long_type, kwargs):
        cmd = "CREATE --type=%s" % long_type
        for k, v in kwargs.items():
            cmd += " %s=%s" % (k, v)
        return json.loads(self(cmd))

    def update(self, long_type, kwargs, name=None, identity=None):
        cmd = 'UPDATE --type=%s' % long_type
        if identity is not None:
            cmd += " --identity=%s" % identity
        elif name is not None:
            cmd += " --name=%s" % name
        for k, v in kwargs.items():
            cmd += " %s=%s" % (k, v)
        return json.loads(self(cmd))

    def delete(self, long_type, name=None, identity=None):
        cmd = 'DELETE --type=%s' %  long_type
        if identity is not None:
            cmd += " --identity=%s" % identity
        elif name is not None:
            cmd += " --name=%s" % name
        else:
            assert False, "name or identity not supplied!"
        self(cmd)

    def query(self, long_type):
        return json.loads(self('QUERY --type=%s' % long_type))

    def get_log(self, limit=None):
        cmd = 'GET-LOG'
        if (limit):
            cmd += " limit=%s" % limit
        return json.loads(self(cmd))


class MgmtMsgProxy:
    """
    Utility for creating and inspecting management messages
    """
    class _Response:
        def __init__(self, status_code, status_description, body):
            self.status_code        = status_code
            self.status_description = status_description
            if body.__class__ == dict and len(body.keys()) == 2 and 'attributeNames' in body.keys() and 'results' in body.keys():
                results = []
                names   = body['attributeNames']
                for result in body['results']:
                    result_map = {}
                    for i in range(len(names)):
                        result_map[names[i]] = result[i]
                    results.append(MgmtMsgProxy._Response(status_code, status_description, result_map))
                self.attrs = {'results': results}
            else:
                self.attrs = body

        def __getattr__(self, key):
            return self.attrs[key]

    def __init__(self, reply_addr):
        self.reply_addr = reply_addr

    def response(self, msg):
        ap = msg.properties
        return self._Response(ap['statusCode'], ap['statusDescription'], msg.body)

    def query_router(self):
        ap = {'operation': 'QUERY', 'type': 'org.apache.qpid.dispatch.router'}
        return Message(properties=ap, reply_to=self.reply_addr)

    def query_connections(self):
        ap = {'operation': 'QUERY', 'type': 'org.apache.qpid.dispatch.connection'}
        return Message(properties=ap, reply_to=self.reply_addr)

    def query_links(self):
        ap = {'operation': 'QUERY', 'type': 'org.apache.qpid.dispatch.router.link'}
        return Message(properties=ap, reply_to=self.reply_addr)

    def query_link_routes(self):
        ap = {'operation': 'QUERY',
              'type': 'org.apache.qpid.dispatch.router.config.linkRoute'}
        return Message(properties=ap, reply_to=self.reply_addr)

    def query_addresses(self):
        ap = {'operation': 'QUERY',
              'type': 'org.apache.qpid.dispatch.router.address'}
        return Message(properties=ap, reply_to=self.reply_addr)

    def create_link_route(self, name, kwargs):
        ap = {'operation': 'CREATE',
              'type': 'org.apache.qpid.dispatch.router.config.linkRoute',
              'name': name}
        return Message(properties=ap, reply_to=self.reply_addr,
                       body=kwargs)

    def delete_link_route(self, name):
        ap = {'operation': 'DELETE',
              'type': 'org.apache.qpid.dispatch.router.config.linkRoute',
              'name': name}
        return Message(properties=ap, reply_to=self.reply_addr)

    def create_connector(self, name, **kwargs):
        ap = {'operation': 'CREATE',
              'type': 'org.apache.qpid.dispatch.connector',
              'name': name}
        return Message(properties=ap, reply_to=self.reply_addr,
                       body=kwargs)

    def delete_connector(self, name):
        ap = {'operation': 'DELETE',
              'type': 'org.apache.qpid.dispatch.connector',
              'name': name}
        return Message(properties=ap, reply_to=self.reply_addr)

    def query_conn_link_routes(self):
        ap = {'operation': 'QUERY',
              'type': 'org.apache.qpid.dispatch.router.connection.linkRoute'}
        return Message(properties=ap, reply_to=self.reply_addr)

    def create_conn_link_route(self, name, kwargs):
        ap = {'operation': 'CREATE',
              'type': 'org.apache.qpid.dispatch.router.connection.linkRoute',
              'name': name}
        return Message(properties=ap, reply_to=self.reply_addr,
                       body=kwargs)

    def delete_conn_link_route(self, name):
        ap = {'operation': 'DELETE',
              'type': 'org.apache.qpid.dispatch.router.connection.linkRoute',
              'name': name}
        return Message(properties=ap, reply_to=self.reply_addr)

    def read_conn_link_route(self, name):
        ap = {'operation': 'READ',
              'type': 'org.apache.qpid.dispatch.router.connection.linkRoute',
              'name': name}
        return Message(properties=ap, reply_to=self.reply_addr)


class TestTimeout:
    """
    A callback object for MessagingHandler class
    parent: A MessagingHandler with a timeout() method
    """
    __test__ = False

    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


class PollTimeout:
    """
    A callback object for MessagingHandler scheduled timers
    parent: A MessagingHandler with a poll_timeout() method
    """

    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.poll_timeout()


def get_link_info(name, address):
    """
    Query the router at address for the status and statistics of the named link
    """
    qdm = QdManager(address=address)
    rc = qdm.query('org.apache.qpid.dispatch.router.link')
    for item in rc:
        if item.get('name') == name:
            return item
    return None


def has_mobile_dest_in_address_table(address, dest):
    qdm = QdManager(address=address)
    rc = qdm.query('org.apache.qpid.dispatch.router.address')
    has_dest = False
    for item in rc:
        if dest in item.get("name"):
            has_dest = True
            break
    return has_dest


def get_inter_router_links(address):
    """
    Return a list of all links with type="inter-router
    :param address:
    """
    inter_router_links = []
    qdm = QdManager(address=address)
    rc = qdm.query('org.apache.qpid.dispatch.router.link')
    for item in rc:
        if item.get("linkType") == "inter-router":
            inter_router_links.append(item)

    return inter_router_links


class Timestamp:
    """
    Time stamps for logging.
    """

    def __init__(self):
        self.ts = datetime.now()

    def __str__(self):
        return self.ts.strftime("%Y-%m-%d %H:%M:%S.%f")


class Logger:
    """
    Record an event log for a self test.
    May print per-event or save events to be printed later.
    Pytest will automatically collect the logs and will dump them for a failed test
    Optional file opened in 'append' mode to which each log line is written.
    """

    def __init__(self,
                 title: str = "Logger",
                 print_to_console: bool = False,
                 save_for_dump: bool = True,
                 python_log_level: Optional[int] = logging.DEBUG,
                 ofilename: Optional[str] = None) -> None:
        self.title = title
        self.print_to_console = print_to_console
        self.save_for_dump = save_for_dump
        self.python_log_level = python_log_level
        self.ofilename = ofilename
        self.logs: List[Tuple[Timestamp, str]] = []

    def log(self, msg):
        ts = Timestamp()
        if self.save_for_dump:
            self.logs.append((ts, msg))
        if self.print_to_console:
            print("%s %s" % (ts, msg))
            sys.stdout.flush()
        if self.python_log_level is not None:
            logging.log(self.python_log_level, f"{ts} {self.title}: {msg}")
        if self.ofilename is not None:
            with open(self.ofilename, 'a') as f_out:
                f_out.write("%s %s\n" % (ts, msg))
                f_out.flush()

    def dump(self):
        print(self)
        sys.stdout.flush()

    def __str__(self):
        lines = [self.title]
        for ts, msg in self.logs:
            lines.append("%s %s" % (ts, msg))
        res = str('\n'.join(lines))
        return res


def curl_available():
    """
    Check if the curl command line tool is present on the system.
    Return a tuple containing the version if found, otherwise
    return false.
    """
    popen_args = ['curl', '--version']
    try:
        process = Process(popen_args,
                          name='curl_check',
                          stdout=PIPE,
                          expect=None,
                          universal_newlines=True)
        out = process.communicate()[0]
        if process.returncode == 0:
            # return curl version as a tuple (major, minor[,fix])
            # expects --version outputs "curl X.Y.Z ..."
            return tuple([int(x) for x in out.split()[1].split('.')])
    except:
        pass
    return False


def run_curl(args, input=None, timeout=TIMEOUT):
    """
    Run the curl command with the given argument list.
    Pass optional input to curls stdin.
    Return tuple of (return code, stdout, stderr)
    """
    popen_args = ['curl'] + args
    if timeout is not None:
        popen_args = popen_args + ["--max-time", str(timeout)]
    stdin_value = PIPE if input is not None else None
    with subprocess.Popen(popen_args, stdin=stdin_value, stdout=PIPE,
                          stderr=PIPE, universal_newlines=True) as p:
        out = p.communicate(input, timeout)
        return p.returncode, out[0], out[1]
