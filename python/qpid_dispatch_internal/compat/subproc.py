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

"""
Implementations of some handy subprocess functions missing in python 2.6
"""

from subprocess import *

try:
    from subprocess import check_output
except ImportError:
    def check_output(args, stdin=None, stderr=None, shell=False, universal_newlines=False, **kwargs):
        """
        Run command args and return its output as a byte string.
        kwargs are passed through to L{subprocess.Popen}
        @return: stdout of command (mixed with stderr if stderr=STDOUT)
        @raise L{CalledProcessError}: If command returns non-0 exit status.
        """
        if "stdout" in kwargs:
            raise ValueError("Must not specify stdout in check_output")
        p = Popen(args, stdout=PIPE, stdin=stdin, stderr=stderr, shell=shell, universal_newlines=universal_newlines, **kwargs)
        out, err = p.communicate()
        if p.returncode:
            e = CalledProcessError(p.returncode, args)
            e.output = err or out
            raise e
        return out
