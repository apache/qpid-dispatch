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

design_doc = '''
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
2021-06-17 21:23:42.768158 -0400 SERVER (notice) Operational, 4 Threads Running (process ID 221746) (/home)

# Router is stopping
2021-06-17 21:24:00.328465 -0400 SERVER (notice) Shut Down (/home)
'''


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
    BUCKET_LIMITS = [0, 1, 10, 100, 1000, 10000, 100000, 10000000]
    BUCKET_LIMITS_LEN = len(BUCKET_LIMITS)

    # Try to make raw csv output legible in on its own
    # A column or row title consists of "acq:" or "use:" plus the title text
    CSV_COL_WIDTH = 12
    CSV_TITLE_WIDTH = 8
    TITLE_BLANK = ' ' * CSV_COL_WIDTH
    TITLE_TOP_LEFT = " acquire uS:"
    TITLES = ["       0", "       1", "     2-9", "   10-99",
              " 100-999", "  1k-10k", "10k-100k", "100k-10m"]
    TITLE_TOTAL = "       Total"

    # Lock pseudo log file header for Scraper
    LOCK_SERVER_CONTAINER_LINE = None

    # Locating server start and stop in router log file
    SERVER_START_CONTENT = 'SERVER (notice) Operational'
    SERVER_STOP_CONTENT  = 'SERVER (notice) Shut Down'

    # Discovered server start and stop times
    START_DATETIME = None
    STOP_DATETIME = None

    # Computed server start and stop uS timestamps
    # Required to exclude router startup and shutdown unusual mutex
    # lock patterns.
    START_US = None
    STOP_US = None

    # Locating the core thread TID and timestamps in the router log file
    CORE_THREAD_TID_CONTENT = "(critical) Core thread TID:"

    # Locating phony lock that tracks core thread idle time via cond_wait
    CORE_THREAD_IDLE_LOCK = "CORE_IDLE"

    # Core TID specified in command line or discovered in router log file
    CORE_TID = None
    # Computed from timestamps in core_thread_tid log line
    BASE_DATETIME = None

    deliveries_hidden = 0     # Show with CLI arg --verbose
    start_stop_hidden = 0     # Show with CLI arg --include-start-stop

    collection_times_first = 0
    collection_times_last = 0

    total_duration = 0  # uS int


class StatsStorage(object):
    """
    Store per-thread array of counts
    """
    def __init__(self, common, store_type, title, numericId):
        self.common = common
        self.store_type = store_type  # 'Thread' or 'Mutex'
        self.title = title
        self.numeric_id = numericId  # simple int
        # buckets holds event counts, buckets_us holds accumulated times in microseconds
        self.buckets = [[0 for i in range(common.BUCKET_LIMITS_LEN)] for j in range(common.BUCKET_LIMITS_LEN)]
        self.buckets_us = [[0 for i in range(common.BUCKET_LIMITS_LEN)] for j in range(common.BUCKET_LIMITS_LEN)]
        self.total_locks = 0
        self.total_acquire_us = 0
        self.total_use_us = 0
        self.acquire_min = 1000000
        self.acquire_max = 0
        self.use_min = 1000000
        self.use_max = 0

    def bucket_of(self, value):
        for i in range(self.common.BUCKET_LIMITS_LEN):
            if value <= self.common.BUCKET_LIMITS[i]:
                return i
        print("Questionable lock/hold value: %d" % value)
        return self.common.BUCKET_LIMITS_LEN - 1

    def add(self, acquire, use):
        col = self.bucket_of(acquire)
        row = self.bucket_of(use)
        self.buckets[col][row] += 1
        self.buckets_us[col][row] += use + acquire
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

    def summary_title(self):
        #      123456789012345678901234567890 12345678 12345678 12345678 12345678 12345678 12345678 12345678 12345678 12345678 1234567890 12345678
        print("                                             Min      Max     Mean    Total  Total %      Min      Max     Mean      Total    Total")
        print("                         Title, N locks, Acquire, Acquire, Acquire, Acquire, Acquire,     Use,     Use,  Use uS,       Use,   Use %")
        print("------------------------------ -------- -------- -------- -------- -------- -------- -------- -------- -------- ---------- --------")

    def summary(self):
        avg_acq = float(self.total_acquire_us) / (float(self.total_locks) if self.total_locks > 0 else 1.0)
        acq_pct =(float(self.total_acquire_us) / float(self.common.total_duration)) * 100.0
        avg_use = float(self.total_use_us)     / (float(self.total_locks) if self.total_locks > 0 else 1.0)
        use_pct =(float(self.total_use_us)     / float(self.common.total_duration)) * 100.0
        print("%-30s,%8d,%8d,%8d,%8.2f,%8d,%8.2f,%8d,%8d,%8.2f,%10d,%8.2f" %
              (self.title, self.total_locks,
               self.acquire_min, self.acquire_max, avg_acq, self.total_acquire_us, acq_pct,
               self.use_min,     self.use_max,     avg_use, self.total_use_us, use_pct))

    def dump(self, title_prefix, show_events):
        print()
        if show_events:
            print("%s %s - Event counts (not accumulated uS). Total elapsed lock usage uS = %d" %
                  (title_prefix, self.title, self.total_use_us))
            buckets = self.buckets
        else:
            print("%s %s - Accumulated mutex usage (uS). Total locks = %d" %
                  (title_prefix, self.title, self.total_locks))
            buckets = self.buckets_us
        print()
        print(self.common.TITLE_TOP_LEFT + ',', end='')
        for col in range(self.common.BUCKET_LIMITS_LEN):
            print("     %s," % (self.common.TITLES[col]), end='')
        print(' ' + self.common.TITLE_TOTAL)

        for row in range(self.common.BUCKET_LIMITS_LEN - 1, 0, -1):
            print("use:%s, " % (self.common.TITLES[row]), end='')
            rtotal = 0
            for col in range(self.common.BUCKET_LIMITS_LEN):
                print("%12d, " % (buckets[col][row]), end='')
                rtotal += buckets[col][row]
            print("%12d" % rtotal)

        gtotal = 0
        print(self.common.TITLE_TOTAL + ', ', end='')
        for col in range(self.common.BUCKET_LIMITS_LEN):
            ctotal = 0
            for row in range(self.common.BUCKET_LIMITS_LEN):
                ctotal += buckets[col][row]
            print("%12d, " % ctotal, end='')
            gtotal += ctotal
        print("%12d" % gtotal)


def usec_log_timestamp(common, usecs):
    ntime = usec_to_dt(common, usecs)
    return ntime.strftime('%Y-%m-%d %H:%M:%S.%f')


def usec_to_dt(common, usecs):
    return common.BASE_DATETIME + timedelta(microseconds=usecs)


def process_core_line(common, line):
    # Derive core TID and offset from a log line
    # 2021-06-17 21:23:42.750591 -0400 ROUTER_CORE (critical) Core thread TID:     0x1a374d0 start. This log timestamp corresponds to lock/unlock report timestamp 294674283709 (/home ...
    # line_time = 2021-06-17 21:23:42.750591
    # core_tid  = 0x1a374d0
    # ts        = 294674283709
    # t_offset  = int(ts)
    # base_time = line_time - t_offset
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
    timestamps so that local analysis may exclude insane lock hold patterns
    during startup and shutdown.
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
                    delta = timedelta(microseconds=100)
                    assert base_dt < common.BASE_DATETIME + delta
                    assert base_dt > common.BASE_DATETIME - delta
            elif common.SERVER_START_CONTENT in line:
                assert common.START_DATETIME is None
                common.START_DATETIME = datetime.strptime(line[:26], '%Y-%m-%d %H:%M:%S.%f')
            elif common.SERVER_STOP_CONTENT in line:
                assert common.STOP_DATETIME is None
                common.STOP_DATETIME = datetime.strptime(line[:26], '%Y-%m-%d %H:%M:%S.%f')


def is_interesting(common, acquire, use, lock_name, is_core, verbose):
    if is_core:
        if acquire >= common.INTERESTING_CORE_ACQUIRE or use >= common.INTERESTING_CORE_USE:
            # could be interesting
            if lock_name == common.CORE_THREAD_IDLE_LOCK:
                return verbose
            else:
                return True
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
                   help='Show suppressed mutex data: disposition locks')
    p.add_argument("--include-start-stop",
                   action='store_true',
                   help='Normally the mutex activity during router server startup and shutdown is excluded. This switch includes it.')
    p.add_argument('--include-core-idle',
                   action='store_true',
                   help='Include core thread idle time in core thread stats')
    p.add_argument('--tables-show-event-counts',
                   action='store_true',
                   help='Normally CSV tables show accumulated microsecond times. This switch shows event instance counts instead.')
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

    core_thread_core_idle = StatsStorage(common, "Mutex", "core_thread_core_idle", 0)

    with open(args.infile) as f:
        for line in f:  # files are routinely > 1 gigabyte. heads up.
            fields = line.split(' ')
            fields = [x for x in fields if x]
            if fields[common.FLD_L_OR_UNL] == "UNLOCK":
                if not args.include_start_stop:
                    line_dt = usec_to_dt(common, int(fields[common.FLD_TIMESTAMP]))
                    if line_dt < common.START_DATETIME or line_dt > common.STOP_DATETIME:
                        common.start_stop_hidden += 1
                        continue

                # Collection limits
                if common.collection_times_first == 0:
                    common.collection_times_first = fields[common.FLD_TIMESTAMP]
                common.collection_times_last =  fields[common.FLD_TIMESTAMP]

                # Thread id
                tid = fields[common.FLD_TID]  # thread ID
                is_core = tid == common.CORE_TID

                # Lock usage
                acquire = int(fields[common.FLD_ACQUIRE])
                use = int(fields[common.FLD_USE])

                # Lock name
                lock_stack = fields[common.FLD_FIRST_LOCK:common.FLD_LAST_LOCK]
                lock_name = lock_stack[-1]

                # Accumulate per thread
                # 'do_this' controls adding lock data to general thread/mutex tables
                # The special CORE_IDLE lock is locked when the core is idle, not when
                # the core is busy using it. Including it skews the lock numbers.
                do_this = True
                title = "#%d %s" % (len(thread_data), tid)
                if is_core:
                    title += " CORE"
                    if lock_name == common.CORE_THREAD_IDLE_LOCK:
                        # always add to kept structure
                        core_thread_core_idle.add(acquire, use)
                        do_this = args.include_core_idle
                if do_this:
                    if tid not in thread_data:
                        thread_data[tid] = StatsStorage(common, "Thread", title, len(thread_data))
                    thread_data[tid].add(acquire, use)

                # Accumulate per mutex
                if do_this and (args.verbose or not lock_name.startswith("delivery-")):
                    if lock_name not in lock_data:
                        title = "#%d %s" % (len(lock_data), lock_name)
                        lock_data[lock_name] = StatsStorage(common, "Mutex", title, len(lock_data))
                    lock_data[lock_name].add(acquire, use)
                else:
                    common.deliveries_hidden += 1

                # Collect interesting lines
                if is_interesting(common, acquire, use, lock_name, is_core, args.include_core_idle):
                    interesting_lines.append(fields)

    print()
    print("Analysis summary")
    print("================")
    print()

    print("CLI infile  : %s" % os.path.abspath(args.infile))
    print("CLI logfile : %s" % os.path.abspath(args.logfile))
    print()

    start_ticks = int(common.collection_times_first)
    stop_ticks = int(common.collection_times_last)
    common.total_duration = stop_ticks - start_ticks
    print("Collection %s locks before server started and after server stopped." %
          ("includes" if args.include_start_stop else "excludes"))
    print("Collection  begin time: %s, ticks: %d" % (usec_log_timestamp(common, start_ticks), start_ticks))
    print("Collection    end time: %s, ticks: %d" % (usec_log_timestamp(common, stop_ticks), stop_ticks))
    print("Collection duration uS: %d" % common.total_duration)
    print()
    print("Threads reported: %d" % len(thread_data))
    print("Mutexes reported: %d" % len(lock_data))
    print()
    print("Core thread id     : %s" % common.CORE_TID)
    if core_thread_core_idle.total_locks > 0:
        try:
            core_idle = float(core_thread_core_idle.total_use_us) / float(common.total_duration)
            print("Core thread busy %% : %6.2f" % (100.0 - float(core_idle * 100.0)))
        except KeyError:
            print("Core thread busy unknown.")
    else:
        print("Core thread busy unknown. No CORE_IDLE locks taken by core thread.")

    print()
    if common.deliveries_hidden > 0:
        print("%d Delivery mutex lock summaries hidden. Expose with --verbose CLI arg."
              % common.deliveries_hidden)
    if common.start_stop_hidden > 0:
        print("%d Mutex locks during server start and stop hidden. Expose with --include-start-stop CLI arg."
              % common.start_stop_hidden)

    print()
    print("Per thread lock summary")
    print("=======================")
    print()

    need_title = True
    for key, value in thread_data.items():
        if need_title:
            value.summary_title()
            need_title = False
        value.summary()

    print()
    print("Per mutex lock summary")
    print("=======================")
    print()

    need_title = True
    for key, value in lock_data.items():
        if need_title:
            value.summary_title()
            need_title = False
        value.summary()

    print()
    print("Per thread lock details")
    print("=======================")
    print()

    for key, value in thread_data.items():
        value.dump("Thread", args.tables_show_event_counts)
        print("")

    print()
    print("Per mutex lock details")
    print("=======================")
    print()

    print("Core thread idle mutex wait lock")
    print("--------------------------------")
    print()
    core_thread_core_idle.dump("CORE IDLE", False)

    print()
    print("Other locks")
    print("-----------")
    print()

    for key, value in lock_data.items():
        value.dump("Mutex", args.tables_show_event_counts)

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
              (usec_log_timestamp(common, ts), ts, tid, acquire, use, acquire + use, lock_stack))

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
