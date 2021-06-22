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

from datetime import timedelta
from datetime import datetime

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

A special log line reveals the core TID and has two timestamps: the normal log timestamp
and the monotonic uS timestamp. From the timestamps a base time is computed.
  base_time = full_log_timestamp - monotonic_uS
  
Then the base time is used to convert the lock/unlock uS values into human-readable full log timestamps.

# Server preface with container name
2021-06-17 21:23:42.747976 -0400 SERVER (info) Container Name: A (/home/chug/git/qpid-dispatch/src/server.c:1373)

# Router is started
2021-06-17 21:23:42.768158 -0400 SERVER (notice) Operational, 4 Threads Running (process ID 221746) (/home

# Router is stopping
2021-06-17 21:24:00.328465 -0400 SERVER (notice) Shut Down (/home

"""

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

    INTERESTING_ACQUIRE_THRESHOLD = 10000
    INTERESTING_USE_THRESHOLD     = 10000
    INTERESTING_CORE_ACQUIRE      = 1000
    INTERESTING_CORE_USE          = 5000

    # CSV buckets for acq: acquisition and use: usage
    BUCKET_LIMITS = [0, 1, 10, 100, 1000, 10000, 100000, 1000000]
    BUCKET_LIMITS_LEN = len(BUCKET_LIMITS)

    # Try to make raw csv output legible in on its own
    # A column or row title consists of "acq:" or "use:" plus the title text
    CSV_COL_WIDTH = 12
    CSV_TITLE_WIDTH = 8
    TITLE_BLANK = ' ' * CSV_COL_WIDTH
    TITLE_TOP_LEFT = " acquire uS:"
    TITLES = ["       0", "       1", "     2-9", "   10-99",
              " 100-999", "  1k-10k", "10k-100k", " 100k-1m"]
    TITLE_TOTAL = "       Total"

    # Lock pseudo log file header for Scraper
    LOCK_SERVER_CONTAINER_LINE = None

    # Locating server start and stop in router log file
    SERVER_START_CONTENT = 'SERVER (notice) Operational'
    SERVER_STOP_CONTENT  = 'SERVER (notice) Shut Down'

    # Discovered server start and stop times
    START_DATETIME = None
    STOP_DATETIME = None

    # Locating the core thread TID and timestamps in the router log file
    CORE_THREAD_TID_CONTENT = "(critical) Core thread TID:"

    # Core TID specified in command line or discovered in router log file
    CORE_TID = None
    # Computed from timestamps in core_thread_tid log line
    BASE_DATETIME = None

    deliveries_hidden = 0  # Show with --verbose CLI arg

class StatsStorage(object):
    """
    Store per-thread array of counts
    """
    def __init__(self, common, store_type, title, numericId):
        self.common = common
        self.store_type = store_type  # 'Thread' or 'Mutex'
        self.title = title
        self.numeric_id = numericId  # simple int
        self.buckets = [[0 for i in range(self.common.BUCKET_LIMITS_LEN)]
                           for j in range(self.common.BUCKET_LIMITS_LEN)]
        self.total_acquire = 0
        self.total_use = 0

    def bucket_of(self, value):
        for i in range(self.common.BUCKET_LIMITS_LEN):
            if value <= self.common.BUCKET_LIMITS[i]:
                return i
        print("Questionable lock/hold value: %d" % value)
        return self.common.BUCKET_LIMITS_LEN

    def add(self, acquire, use):
        col = self.bucket_of(acquire)
        row = self.bucket_of(use)
        self.buckets[col][row] += 1
        self.total_acquire += acquire
        self.total_use += use

    def dump(self, title_prefix):
        print()
        print("%s %s - total acquire uS: %d, total use uS: %s" %
              (title_prefix, self.title, self.total_acquire, self.total_use))
        print()
        print(self.common.TITLE_TOP_LEFT + ',', end='')
        for col in range(self.common.BUCKET_LIMITS_LEN):
            print ("     %s," % (self.common.TITLES[col]), end='')
        print(' ' + self.common.TITLE_TOTAL)

        for row in range(self.common.BUCKET_LIMITS_LEN-1, 0, -1):
            print("use:%s, " % (self.common.TITLES[row]), end='')
            rtotal = 0
            for col in range(self.common.BUCKET_LIMITS_LEN):
                print("%12d, " % (self.buckets[col][row]), end='')
                rtotal += self.buckets[col][row]
            print("%12d" % rtotal)
            # Does 'use: 1' ever print anything? If not then suppress it
            if row == 1:
                assert rtotal == 0

        gtotal = 0
        print(self.common.TITLE_TOTAL + ', ', end='')
        for col in range(self.common.BUCKET_LIMITS_LEN):
            ctotal = 0
            for row in range(self.common.BUCKET_LIMITS_LEN):
                ctotal += self.buckets[col][row]
            print("%12d, " % ctotal, end='')
            gtotal += ctotal
        print("%12d" % gtotal)


def usec_log_timestamp(common, usecs):
    ntime = common.BASE_DATETIME + timedelta(microseconds=usecs)
    return ntime.strftime('%Y-%m-%d %H:%M:%S.%f')


def process_core_line(common, line):
    # Derive core TID and offset from a log line
    # 2021-06-17 21:23:42.750591 -0400 ROUTER_CORE (critical) Core thread TID:     0x1a374d0 start. This log timestamp corresponds to lock/unlock report timestamp 294674283709 (/home ...
    line_time = datetime.strptime(line[:26], '%Y-%m-%d %H:%M:%S.%f')
    tid_pos = line.find(common.CORE_THREAD_TID_CONTENT) + len(common.CORE_THREAD_TID_CONTENT)
    core_tid = line[tid_pos:tid_pos + 14].strip()
    ts_pos = line.find("report timestamp ")
    ts = line[ts_pos:].split()[2]
    t_offset = int(ts)
    dt_delta = timedelta(microseconds=t_offset)
    base_time = line_time - dt_delta
    return core_tid, base_time


def process_logfile(common, filename, container_name):
    '''
    If the router log file exists: Find core thread ID and the timestamp
    deltas between the log timestamp and the thread-debug uS timestamp.
    Find the "router started" and "router shutting down"
    timestamps so that local analysis may exclude insane lock hold times
    before startup and after shutdown.
    :param filename:
    :return: (core-TID, timestamp_delta)
    '''
    # This log line defines the core TID and offsets
    governing_log_line = None

    # Derived core TID and offset
    core_tid = None
    base_dt = None

    with open(filename) as f:
        # Construct the header line required by scraper
        # Grab the timestamp from the first line of the router log
        line = f.readline()
        common.LOCK_SERVER_CONTAINER_LINE = \
            line[:26] + " SERVER (info) Container Name: " + container_name + " (/home)"

        for line in f:
            if common.CORE_THREAD_TID_CONTENT in line:
                if common.CORE_TID is None:
                    # Accept first instance
                    (common.CORE_TID, common.BASE_DATETIME) = process_core_line(common, line)
                else:
                    # Remaining instances must match to prevent multiple router
                    # runs piling differing values into the same router log file.
                    core_tid, base_dt = process_core_line(common, line)
                    delta = timedelta(microseconds=100) # seen two timestamps vary by < 10 uS. Apply some slop.
                    assert base_dt < common.BASE_DATETIME + delta
                    assert base_dt > common.BASE_DATETIME - delta
            elif common.SERVER_START_CONTENT in line:
                assert common.START_DATETIME is None
                common.START_DATETIME = datetime.strptime(line[:26], '%Y-%m-%d %H:%M:%S.%f')
            elif common.SERVER_STOP_CONTENT in line:
                assert common.STOP_DATETIME is None
                common.STOP_DATETIME = datetime.strptime(line[:26], '%Y-%m-%d %H:%M:%S.%f')


def is_interesting(common, acquire, use, is_core):
    if is_core:
        return acquire >= common.INTERESTING_CORE_ACQUIRE or use >= common.INTERESTING_CORE_USE
    else:
        return acquire >= common.INTERESTING_ACQUIRE_THRESHOLD or use >= common.INTERESTING_USE_THRESHOLD


def main_except(argv):

    p = argparse.ArgumentParser()
    p.add_argument('--infile', '-i',
                   help='Required thread-debug .out filename which is input to this progrqm')
    p.add_argument('--logfile', '-l',
                   help="Optional router .log filename which is input to this program")
    p.add_argument('--verbose',
                   action='store_true',
                   help='Show suppressed mutex data')
    del argv[0]
    args = p.parse_args(argv)

    if args.infile is None:
        raise Exception("User must specify --infile filename")
    if args.logfile is None:
        raise Exception("User must specify --logfile filename")

    common = Common()

    basename = os.path.basename(args.infile)
    process_logfile(common, args.logfile, os.path.splitext(basename)[0])

    thread_data = {}
    lock_data = {}
    interesting_lines = []  # lines split into fields

    with open(args.infile) as f:
        for line in f:  # files are routinely > 1 gigabyte. heads up.
            fields = line.split(' ')
            fields = [x for x in fields if x]
            if fields[common.FLD_L_OR_UNL] == "UNLOCK":
                # Thread id
                tid = fields[common.FLD_TID]  # thread ID
                is_core = tid == common.CORE_TID

                # Lock usage
                acquire = int(fields[common.FLD_ACQUIRE])
                use = int(fields[common.FLD_USE])

                # Accumulate per thread
                if not tid in thread_data:
                    title = "%s #%d" % (tid, len(thread_data))
                    if is_core:
                        title += " CORE"
                    thread_data[tid] = StatsStorage(common, "Thread", title, len(thread_data))
                thread_data[tid].add(acquire, use)

                # Accumulate per mutex
                lock_stack = fields[common.FLD_FIRST_LOCK:common.FLD_LAST_LOCK]
                lock_name = lock_stack[-1]
                if args.verbose or not lock_name.startswith("delivery-"):
                    if not lock_name in lock_data:
                        title = "%s #%d" % (lock_name, len(lock_data))
                        lock_data[lock_name] = StatsStorage(common, "Mutex", title, len(lock_data))
                    lock_data[lock_name].add(acquire, use)
                else:
                    common.deliveries_hidden += 1

                # Collect interesting lines
                if is_interesting(common, acquire, use, is_core):
                    interesting_lines.append(fields)

    print()
    print("Per thread lock summary")
    print("=======================")
    print()
    for key, value in thread_data.items():
        value.dump("Thread")
        print("")

    print()
    print("Per mutex lock summary")
    print("=======================")
    print()

    for key, value in lock_data.items():
        value.dump("Mutex")
    if common.deliveries_hidden > 0:
        print("\nDelivery mutex lock summaries hidden: %d. Expose with --verbose CLI arg." % common.deliveries_hidden)

    print()
    print("Interesting observations made during processing")
    print("===============================================")
    print()

    for fields in interesting_lines:
        ts = int(fields[common.FLD_TIMESTAMP])  # uS from arbitrary starting point like last boot
        tid = fields[common.FLD_TID]  # thread ID
        if tid == common.CORE_TID:
            tid = fields[common.FLD_TID] + " CORE"
        acquire = int(fields[common.FLD_ACQUIRE])
        use = int(fields[common.FLD_USE])
        lock_stack = fields[common.FLD_FIRST_LOCK:common.FLD_LAST_LOCK]
        print("%s %d Thread %s: acquire:%6d, use:%6d, total:%6d, locks:%s" %
              (usec_log_timestamp(common, ts), ts, tid, acquire, use, acquire+use, lock_stack))

    print()
    print("Interesting observations in logfile format")
    print("==========================================")
    print()
    print("%s" % common.LOCK_SERVER_CONTAINER_LINE)  # Required for scraper
    for fields in interesting_lines:
        ts = int(fields[common.FLD_TIMESTAMP])  # uS from arbitrary starting point like last boot
        tid = fields[common.FLD_TID]  # thread ID
        if tid == common.CORE_TID:
            tid = fields[common.FLD_TID] + " CORE"
        acquire = int(fields[common.FLD_ACQUIRE])
        use = int(fields[common.FLD_USE])
        lock_stack = fields[common.FLD_FIRST_LOCK:common.FLD_LAST_LOCK]
        start_lock = usec_log_timestamp(common, ts - acquire - use)
        locked     = usec_log_timestamp(common, ts - use)
        end_lock   = usec_log_timestamp(common, ts)
        lock_time = "acquire: %d use: %d total: %d" % (acquire, use, acquire + use)
        print("%s LOCK (notice) %d %s mutex_lock    start %s %s" % (start_lock, ts - acquire - use, tid, lock_stack, lock_time))
        print("%s LOCK (notice) %d %s mutex_lock acquired %s %s" % (locked,     ts - use          , tid, lock_stack, lock_time))
        print("%s LOCK (notice) %d %s mutex_lock released %s %s" % (end_lock,   ts                , tid, lock_stack, lock_time))


def main(argv):
    try:
        main_except(argv)
        return 0
    except Exception as e:
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
