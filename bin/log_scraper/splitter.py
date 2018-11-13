#!/usr/bin/env python

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

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

import traceback
import test_data as td


class Splitter():
    @staticmethod
    def split(line):
        """
        Split a log line into fields.
         * allow commas and spaces in quoted strings.
         * split on ', ' and on ' '.
           strip trailing commas between fields.
         * quoted fields must have both quotes
        :param line:
        :return:
        """
        result = []
        indqs = False
        pending_comma = False
        res = ""
        for i in range(len(line)):
            c = line[i]
            if c == '\"':
                if pending_comma:
                    res += ','
                    pending_comma = False
                indqs = not indqs
                res += c
            elif c == ',':
                if pending_comma:
                    res += c
                pending_comma = True
            elif c == ' ':
                if indqs:
                    if pending_comma:
                        res += ','
                        pending_comma = False
                    res += c
                else:
                    if res != '':
                        if pending_comma:
                            pending_comma = False
                        result.append(res)
                        res = ''
            else:
                res += c
        if res != '':
            result.append(str(res))
        if indqs:
            raise ValueError("SPLIT ODD QUOTES: %s", line)
        # print ("SPLIT: line: %s" % line)
        # print ("SPLIT: flds: %s" % result)
        return result


if __name__ == "__main__":

    try:
        for line in td.TestData().data():
            if "transfer" not in line:
                print(Splitter.split(line))
                print()
            else:
                pass  # splitter does not split transfers
        pass
    except:
        traceback.print_exc(file=sys.stdout)
        pass
