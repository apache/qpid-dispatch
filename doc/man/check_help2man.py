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

"""Check that man page checkedd in to source tree matches genated file"""

import sys, os

srcdir, bindir = sys.argv[1:3]
files = sys.argv[3:]
failed = []

def normalize(f):
    f.readline()
    f.readline()
    return f.read()

for f in files:
    src = os.path.join(srcdir, f+".in")
    gen = os.path.join(bindir, f)
    if normalize(open(src)) != normalize(open(gen)): failed.append((gen, src))

if failed:
    print "ERROR: generated man pages do not match checked in versions. To fix do: "
    for gen, src in failed:
        print "  cp  %s %s" % (gen, src)
    sys.exit(1)
