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

import sys
import traceback

class TestData():
    '''
    Extract list of test log lines from a data file.
    The file holds literal log lines from some noteworthy test logs.
    Embedding the lines as a python source code data statement involves escaping
    double quotes and runs the risk of corrupting the data.
    '''
    def __init__(self, fn="test_data/test_data.txt"):
        with open(fn, 'r') as f:
            self.lines = [line.rstrip('\n') for line in f]

    def data(self):
        return self.lines


if __name__ == "__main__":

    try:
        datasource = TestData()
        for line in datasource.data():
            print (line)
        pass
    except:
        traceback.print_exc(file=sys.stdout)
        pass
