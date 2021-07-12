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


class Common:
    FLD_TIMESTAMP  = 0
    FLD_TID_TXT    = 1
    FLD_TID        = 2
    FLD_L_OR_UNL   = 3
    FLD_FIRST_LOCK = 4

    STACK_CSV_SEP = "->"

class Stats(object):
    """
    Store per-lock counts
    """
    def __init__(self, lock_stack):
        self.lock_stack = lock_stack
        self.total_locks = 0
        self.total_acquire_us = 0
        self.total_use_us = 0
        self.acquire_min = 10000000
        self.acquire_max = 0
        self.use_min = 10000000
        self.use_max = 0

    def add(self, acquire, use):
        self.total_locks += 1
        self.total_acquire_us += acquire
        self.total_use_us += use
        if acquire < self.acquire_min:
            self.acquire_min = acquire
        if acquire > self.acquire_max:
            self.acquire_max = acquire
        if use < self.use_min:
            self.use_min = use
        if use > self.use_max:
            self.use_max = use

    @classmethod
    def show_titles(cls):
        print("lock stack                                          ,       total,  acquire,  acquire,     acquire,      use,      use,         use")
        print("                                                    ,       count,  min(uS),  max(uS),   total(uS),  min(uS),  max(uS),   total(uS)")

    def show(self):
        print("%-52s, %11d, %8d, %8d, %11d, %8d, %8d, %11d" %
              (self.lock_stack, self.total_locks,
               self.acquire_min, self.acquire_max, self.total_acquire_us,
               self.use_min, self.use_max, self.total_use_us))

    def __str__(self):
        return self.lock_stack


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

def main_except():
    common = Common()
    instance_data = {}

    n_lines = 0
    n_unlocks_processed = 0
    n_deliveries_collapsed = 0
    last_lock_range = common.FLD_FIRST_LOCK
    for line in sys.stdin:  # *.out UNLOCK only: 74 M lines, 15 Gb ...
        n_lines += 1
        if n_lines % 500000 == 0:
            print("Processing line %10d" % n_lines)
        fields = line.split(' ')
        fields = [x for x in fields if x]
        if len(fields) < common.FLD_L_OR_UNL:
            continue
        if fields[common.FLD_L_OR_UNL] == "UNLOCK":
            n_unlocks_processed += 1
            # last_lock constant is incorrect for lock output with stack traces
            # search for last lock
            for last_lock_range in range(common.FLD_FIRST_LOCK + 1, len(fields)):
                if "==" in fields[last_lock_range]:
                    break
            # HACK ALERT
            # Having tens of thousands of delivery-NNNNN lock lines is not useful.
            # Collapse them into a single lock stack named "delivery-N"
            if fields[common.FLD_FIRST_LOCK].startswith("delivery-"):
                fields[common.FLD_FIRST_LOCK] = "delivery-N"
                n_deliveries_collapsed += 1
            lock_stack = fields[common.FLD_FIRST_LOCK:last_lock_range]
            T_ACQ = last_lock_range + 4
            T_USE = last_lock_range + 6
            acquire = fields[T_ACQ]
            use = fields[T_USE]
            lock_stack_str = common.STACK_CSV_SEP.join(lock_stack)
            if lock_stack_str not in instance_data:
                instance_data[lock_stack_str] = Stats(lock_stack_str)
            instance_data[lock_stack_str].add(int(acquire), int(use))

    print()
    print("Lock stacks")
    print("===========")
    print()
    print("Found %10d lines total" % n_lines)
    print("Found %10d UNLOCK lines" % n_unlocks_processed)
    print("Found %10d lock stacks" % len(instance_data))
    print("Found %10d delivery-NNNN locks collapsed into one table row" % n_deliveries_collapsed)
    print()
    Stats.show_titles()
    for val in sorted(instance_data):
        instance_data[val].show()

    print()
    print("Possible deadlocks")
    print("==================")
    print()

    # Deadlock inversion detection. Create inversion database.
    lock_inversion_data = defaultdict(set)
    for lock_stack_str in instance_data:
        lock_stack = lock_stack_str.split(common.STACK_CSV_SEP)
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


def main():
    try:
        main_except()
        return 0
    except Exception:
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
