#!/usr/bin/env python3

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

import argparse
import ast
import os
import sys
import traceback

from collections import defaultdict
from datetime import timedelta
from datetime import datetime

class Common():
    FLD_TIMESTAMP  = 0
    FLD_TID_TXT    = 1
    FLD_TID        = 2
    FLD_L_OR_UNL   = 3
    FLD_FIRST_LOCK = 4
    FLD_LAST_LOCK  = -10
    FLD_ACQUIRE    = -6
    FLD_USE        = -4
    FLD_CLOSE      = -1

#
# Usage:
#  cd build
#  make
#  ctest
#
#  # To examine a single router's output
#  lock-stacks.py < some-router.out
#
#  # To examine all routers' output
#  find . -name "*.out" | xargs grep UNLOCK | lock-stacks.py
#

def main_except(argv):
    common = Common()

    # lock inversion data is a set of lists where each list is a lock stack
    lock_stacks = set()

    # first instances helps a user find in which router the lock stack happens
    first_instances = {}

    # total instances indicates how many times the lock stack was seen
    total_instances = defaultdict(lambda:0)

    n_lines = 0
    n_lock_stacks_len_eq_1 = 0
    n_lock_stacks_len_gt_1 = 0
    n_unlocks_processed = 0
    for line in sys.stdin:  # *.out UNLOCK only: 74 M lines, 15 Gb ...
        n_lines += 1
        if n_lines % 500000 == 0:
            print("Processing line %10d" % n_lines)
        fields = line.split(' ')
        fields = [x for x in fields if x]
        if fields[common.FLD_L_OR_UNL] == "UNLOCK":
            n_unlocks_processed += 1
            # last_lock constant is incorrect for lock output with stack traces
            for last_lock in range(common.FLD_FIRST_LOCK, len(fields)):
                if "==" in fields[last_lock]:
                    break
            lock_stack = fields[common.FLD_FIRST_LOCK:last_lock]
            if len(lock_stack) > 1:
                n_lock_stacks_len_gt_1 += 1
                lock_stack_str = ','.join(lock_stack)
                lock_stacks.add(lock_stack_str)
                if lock_stack_str not in first_instances:
                    # store only first instance not every instance
                    first_instances[lock_stack_str] = line.rstrip()
                total_instances[lock_stack_str] += 1
            else:
                n_lock_stacks_len_eq_1 += 1

    print()
    print("Lock stacks")
    print("===========")
    print()
    print("Found %10d lines total" % n_lines)
    print("Found %10d UNLOCK lines" % n_unlocks_processed)
    print("Found %10d lock stacks len >= 2 to process" % len(lock_stacks))
    print("Found %10d lock stacks len == 1 to ignore" % n_lock_stacks_len_eq_1)
    print()
    for val in sorted(lock_stacks):
        print("%-47s : n=%10d : %s" % (val, total_instances[val], first_instances[val]))

    print()
    print("Possible deadlocks")
    print("==================")
    print()

    # Deadlock inversion detection. Create inversion database.
    lock_inversion_data = defaultdict(set)
    for lock_stack_str in lock_stacks:
        lock_stack = lock_stack_str.split(',')
        stack_len = len(lock_stack)
        # Add lock stack to inversion database
        for heldi in range(stack_len-1):
            for locksi in range(heldi + 1, stack_len):
                held = lock_stack[heldi]
                locks = lock_stack[locksi]
                if held not in lock_inversion_data:
                    lock_inversion_data[held] = set()
                lock_inversion_data[held].add(locks)

    # Deadlock inversion detection
    for k, v in lock_inversion_data.items():
        for locks in v:
            if locks in lock_inversion_data:
                if k in lock_inversion_data[locks]:
                    print("Inversion %s and %s" % (k, locks))



def main(argv):
    try:
        main_except(argv)
        return 0
    except Exception as e:
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
