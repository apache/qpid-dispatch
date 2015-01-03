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
Generate markdown man pages by combining a markdown document with the
the --help output of the program.

Extract the options section of help output and convert to markdown format for
inclusion in a man page. Replace the # Options section of the source document
or append it at the end if there is no # Options section.
"""

import re, sys
from subprocess import check_output, STDOUT
from os import path

def help2md(help_out):
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

    result += "# Options\n\n"
    for item in re.finditer(r"%s|%s" % (OPT_HELP, SUBHEAD), help_out, re.IGNORECASE | re.MULTILINE):
        if item.group(3):
            result += "## %s\n\n" % item.group(3).strip() # Sub-heading
        else:
            result += "%s\n:   %s\n\n" % (item.group(1), re.sub("\s+", " ", item.group(2)).strip())
    return result

usage = "Usage: %s manpage_in.md manpage_out.md program [help-args...]"

def main(argv):
    if len(argv) < 4: raise ValueError("Wrong number of arguments: "+usage)
    source, target, program = argv[1], argv[2], argv[3:]
    source_md = open(source).read()
    options_md = help2md(check_output(program, stderr=STDOUT))
    combine_md = re.sub(r"\n# Options.*?(?=(\n# |$))", options_md, source_md, flags=re.IGNORECASE | re.DOTALL)
    upcase_md = re.sub(r"^#+ .*$", lambda m: m.group(0).upper(), combine_md, flags=re.MULTILINE)
    open(target, "w").write(upcase_md)

if __name__ == "__main__":
    main(sys.argv)
