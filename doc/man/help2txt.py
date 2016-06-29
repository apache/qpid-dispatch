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
Convert --help output of a program into asciidoc text format.
"""
from __future__ import print_function
import re, sys
from qpid_dispatch_internal.compat.subproc import check_output, STDOUT, CalledProcessError
from os import path

def help2txt(help_out):
    VALUE = r"(?:[\w-]+|<[^>]+>)"
    DEFAULT = r"(?: +\([^)]+\))?"
    OPTION = r"-[\w-]+(?:[ =]%s)?%s" % (VALUE, DEFAULT) # -opt[(=| )value][(default)]
    OPTIONS = r"%s(?:, *%s)*" % (OPTION, OPTION)        # opt[,opt...]
    HELP = r"(?:[ \t]+\w.*$)|(?:(?:\n[ \t]+[^-\s].*$)+)" # same line or following lines indented.
    OPT_HELP = r"^\s+(%s)(%s)" % (OPTIONS, HELP)
    SUBHEAD = r"^((?: +\w+)*):$"

    options = re.search("^Options:$", help_out, re.IGNORECASE | re.MULTILINE)
    if (options): help_out = help_out[options.end():]
    result = ""

    def heading(text, depth):
        return "%s %s\n\n" % ("="*depth, text)

    for item in re.finditer(r"%s|%s" % (OPT_HELP, SUBHEAD), help_out, re.IGNORECASE | re.MULTILINE):
        if item.group(3):
            result += heading(item.group(3).strip(), 3)
        else:
            result += "%s\n:   %s\n\n" % (item.group(1), re.sub("\s+", " ", item.group(2)).strip())
    return result

def main(argv):
    if len(argv) < 2: raise ValueError("Wrong number of arguments: "+usage)
    program = argv[1:]
    print(help2txt(check_output(program)))

if __name__ == "__main__":
    main(sys.argv)

