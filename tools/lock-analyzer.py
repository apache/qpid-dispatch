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
import os
import sys
import traceback

"""
This program accepts a filename for a thread-debug-cr4 .out file and produces 
a summary of the lock patterns.

Input for a thread locking and unlocking a single lock:

  255398575248 TID:0x7ff9097bee60   LOCK           PYTHON ==> [ LOCK acquire_uS 0 ]
  255398624651 TID:0x7ff9097bee60 UNLOCK           PYTHON <== [ UNLOCK acquire_uS 0 use 49402 total  49402 ]

Input for a thread locking and unlocking two locks:

  255398575145 TID:0x7ff9097bee60   LOCK              LOG ==> [ LOCK acquire_uS 1 ]
  255398575182 TID:0x7ff9097bee60   LOCK              LOG     ENTITY_CACHE ==> [ LOCK acquire_uS 0 ]
  255398575188 TID:0x7ff9097bee60 UNLOCK              LOG     ENTITY_CACHE <== [ UNLOCK acquire_uS 0 use 6 total  6 ]
  255398575193 TID:0x7ff9097bee60 UNLOCK              LOG <== [ UNLOCK acquire_uS 1 use 49 total  50 ]

The v0 goal of this program is to produce a csv text for importing into a spreadsheet and then
to be displayed as numbers or as a graph. 
* The rows are buckets of lock-acquire times, the columns are buckets of lock-total times, 
  and the values are counts of mutex lock-unlock observations for the bucket.
* Separate csv sections are produced for each thread.
"""

FLD_TIMESTAMP  = 0
FLD_TID_TXT    = 1
FLD_TID        = 2
FLD_L_OR_UNL   = 3
FLD_FIRST_LOCK = 4
FLD_LAST_LOCK  = -10
FLD_ACQUIRE    = -6
FLD_USE        = -4
FLD_CLOSE      = -1

REASONABLE_LOCK_CUTOFF = 10000  # lock transaction totals .gt. this are unreasonable

# CSV buckets for acq: acquisition and use: usage
BUCKET_LIMITS = [0, 1, 10, 100, 1000, 10000, 100000, 1000000]
BUCKET_LIMITS_LEN = len(BUCKET_LIMITS)

# Try to make raw csv output legible in on its own
# A column or row title consists of "acq:" or "use:" plus the title text
CSV_COL_WIDTH = 12
CSV_TITLE_WIDTH = 8
TITLE_BLANK = ' ' * CSV_COL_WIDTH
TITLES = ["       0", "       1", "     2-9", "   10-99",
          " 100-999", "  1k-10k", "10k-100k", " 100k-1m"]
TITLE_TOTAL = "       Total"

class PerThread(object):
    """
    Store per-thread array of counts
    """

    def __init__(self, threadId, is_core):
        self.tid = threadId
        self.buckets = [[0 for i in range(BUCKET_LIMITS_LEN)] for j in range(BUCKET_LIMITS_LEN)]
        self.is_core = is_core

    def bucket_of(self, value):
        for i in range(BUCKET_LIMITS_LEN):
            if value <= BUCKET_LIMITS[i]:
                return i
        print("Questionable lock/hold value: %d" % value)
        return BUCKET_LIMITS_LEN

    def add(self, useconds, tid, acquire, use, lock_stack=[]):
        col = self.bucket_of(acquire)
        row = self.bucket_of(use)
        self.buckets[col][row] += 1
        if acquire >= REASONABLE_LOCK_CUTOFF or use >= REASONABLE_LOCK_CUTOFF or \
                (self.is_core and acquire >= 1000):
            core_id = " --CORE--" if self.is_core else ""
            print("Long wait timestamp:%d %s%s acquire: %d use: %d total: %d lock stack: %s" %
                  (useconds, tid, core_id, acquire, use, acquire + use, lock_stack))

    def dump(self):
        print("Thread %s %s" % (self.tid, "--CORE--" if self.is_core else ""))
        print(TITLE_BLANK + ',', end='')
        for col in range(BUCKET_LIMITS_LEN):
            print (" acq:%s," % (TITLES[col]), end='')
        print(TITLE_TOTAL)

        for row in range(BUCKET_LIMITS_LEN-1, -1, -1):
            print("use:%s, " % (TITLES[row]), end='')
            rtotal = 0
            for col in range(BUCKET_LIMITS_LEN):
                print("%12d, " % (self.buckets[col][row]), end='')
                rtotal += self.buckets[col][row]
            print("%12d" % rtotal)

        gtotal = 0
        print(TITLE_TOTAL + ', ', end='')
        for col in range(BUCKET_LIMITS_LEN):
            ctotal = 0
            for row in range(BUCKET_LIMITS_LEN):
                ctotal += self.buckets[col][row]
            print("%12d, " % ctotal, end='')
            gtotal += ctotal
        print("%12d" % gtotal)


def main_except(argv):

    p = argparse.ArgumentParser()
    p.add_argument('--outfile', '-o',
                   help='Required thread-debug .out filename')
    p.add_argument('--core-tid',
                   help='Optional core thread ID')
    del argv[0]
    args = p.parse_args(argv)

    if args.outfile is None:
        raise Exception("User must specify --outfile filename")

    thread_data = {}
    lock_data = {}

    with open(args.outfile) as f:
        print("Exceptional observations made during processing")
        print("===============================================")
        print()
        for line in f:  # files are routinely > 1 gigabyte. heads up.
            fields = line.split(' ')
            fields = [x for x in fields if x]
            if fields[FLD_L_OR_UNL] == "UNLOCK":
                ts = int(fields[FLD_TIMESTAMP])  # uS from arbitrary starting point
                tid = fields[FLD_TID]  # thread ID
                if not tid in thread_data:
                    is_core = tid == args.core_tid
                    thread_data[tid] = PerThread(tid, is_core)
                assert (fields[FLD_CLOSE] == "]\n" )
                acquire = int(fields[FLD_ACQUIRE])
                use = int(fields[FLD_USE])
                lock_stack = fields[FLD_FIRST_LOCK:FLD_LAST_LOCK]
                thread_data[tid].add(ts, tid, acquire, use, lock_stack)

    print()
    print("Per thread lock summary")
    print("=======================")
    print()
    for key, value in thread_data.items():
        value.dump()
        print("")


def main(argv):
    try:
        main_except(argv)
        return 0
    except Exception as e:
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
