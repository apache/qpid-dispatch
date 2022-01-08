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

import optparse
import sys

"""
This program accepts input for a modified Scraper and produces output to create sequence diagrams
at sequencediagram.org.
"""

IDX_DATETIME = 0
IDX_NAME_LEFT = 1
IDX_DIR = 2
IDX_NAME_RIGHT = 3
IDX_PERFORMATIVE = 4
IDX_ROUTER_LINE = 5

# Exactly how to predict how much 'space' asynchronous messages take is vague.
MAGIC_SPACE_NUMBER = 1   # tested with entryspacing 0.1


class log_record:

    def __init__(self, index, line):
        # print("DEBUG input line: ", index, line)
        dateandtime, name_left, direction, name_right, perf, router_line, dummy = line.split('|')
        self.dateandtime = dateandtime.strip()
        self.time = self.dateandtime.split(' ')[1]
        self.index = index
        self.name_left = name_left.strip()
        self.direction = direction.strip()
        self.name_right = name_right.strip()
        self.performative = perf.strip()
        self.router_line = router_line.strip()  # diag
        self.peer_log_rec = None
        dir_out = (direction == '->')  # name_left -> name_right
        self.sentby = name_left.strip() if dir_out else name_right.strip()
        self.rcvdby = name_right.strip() if dir_out else name_left.strip()

    def absorb_peer_rec(self, peer_rec):
        """
        Given peer_rec, see if it is the receiving side of
        this log_rec being sent. If so then cross-link the
        records and return true.
        :param peer_rec:
        :return:
        """
        valid_peer = self.name_left == peer_rec.name_right and \
            self.name_right == peer_rec.name_left and \
            self.direction != peer_rec.direction and \
            self.performative.startswith(peer_rec.performative) and \
            self.peer_log_rec is None and \
            peer_rec.peer_log_rec is None
        if not valid_peer:
            return False
        self.peer_log_rec = peer_rec
        peer_rec.peer_log_rec = self
        return True

    def show_for_sdorg(self, showtime):
        # A single sd.org vector consumes one or two log lines.
        #  One if from an external actor.
        #  Two if a message goes between routers.
        # Leave space where the message is supposed to land in the receiving routers
        if self.peer_log_rec is None:
            if showtime:
                print("%s->%s:%s %s" % (self.sentby, self.rcvdby, self.time, self.performative))
            else:
                print("%s->%s:%s" % (self.sentby, self.rcvdby, self.performative))
        else:
            if self.peer_log_rec.index > self.index:
                linediff = int(self.peer_log_rec.index) - int(self.index)
                space = -MAGIC_SPACE_NUMBER * (linediff - 1)
                if showtime:
                    print("%s->(%s)%s:%s %s" % (self.sentby, str(linediff), self.rcvdby, self.time, self.performative))
                else:
                    print("%s->(%s)%s:%s" % (self.sentby, str(linediff), self.rcvdby, self.performative))
                print("space %s" % (str(space)))
            else:
                print("space %d" % MAGIC_SPACE_NUMBER)

    def sender_receiver(self):
        # Return sender receiver
        return self.sentby, self.rcvdby

    def diag_dump(self):
        cmn = ("index: %d, dateandtime: %s, sentby: %s, rcvdby: %s, performative: %s, router_line: %s" %
               (self.dateandtime, self.index, self.sentby, self.rcvdby, self.performative, self.router_line))
        if self.peer_log_rec is None:
            print(cmn)
        else:
            print("%s, PEER MATCH peer_index: %s, peer_router_log_line: %s" %
                  (cmn, self.peer_log_rec.index, self.peer_log_rec.router_line))


def split_log_file(filename):
    """
    Given a filename, read the file into an array of lines
    :param filename:
    :return:
    """
    if filename == "STDIN" or filename == "" or filename == "-":
        log = sys.stdin
        log_lines = log.read().split("\n")
    else:
        log = open(filename, 'r')
        log_lines = log.read().split("\n")
        log.close()
    return log_lines


def match_logline_pairs(log_recs):
    """
    Given a log line that might be the source of a message to another router,
    search the list of remaining log lines and try to find the router that received
    this message. Then hook them together.
    The cut of Scraper output presented as input to this program may not be complete.
    Messages for which there is no 'match' are drawn as horizontal lines.
    :param log_lines:
    :return:
    """
    for idx in range(len(log_recs) - 2):
        for idx2 in range(idx + 1, len(log_recs) - 1):
            if log_recs[idx].absorb_peer_rec(log_recs[idx2]):
                break


if __name__ == "__main__":
    parser = optparse.OptionParser(usage="%prog [options]",
                                   description="cooks a scraper log snippet into sequence diagram source")
    parser.add_option("-f", "--filename", action="append", help="logfile to use or - for stdin")
    parser.add_option('--timestamp', '-t',
                      action='store_true',
                      help='Include HH:MM:SS.ssssss in output')

    (opts, args) = parser.parse_args()
    if not opts.filename:
        opts.filename = ["-"]

    log_recs = []
    for logfile in opts.filename:
        # print("Parsing: %s" % logfile)
        log_lines = split_log_file(logfile)
        if len(log_lines) >= 2:
            index = 0
            for logline in log_lines:
                if len(logline) > 0:
                    log_recs.append(log_record(index, logline))
                    index += 1
            match_logline_pairs(log_recs)

            # print senders and receivers marking as actor or participant
            names = set()
            for log_rec in log_recs:
                sndr, rcvr = log_rec.sender_receiver()
                if sndr is not None:
                    names.add(sndr)
                if rcvr is not None:
                    names.add(rcvr)
            for name in names:
                if name.startswith("peer"):
                    print("actor %s" % name)
                else:
                    print("participant %s" % name)
            print()

            # process the list of records
            for log_rec in log_recs:
                log_rec.show_for_sdorg(opts.timestamp)

            # diag dump
            #print("\nDiag dump")
            # for log_rec in log_recs:
            #    log_rec.diag_dump()
        else:
            print("Log file has fewer than two lines. I give up.")
        print()
