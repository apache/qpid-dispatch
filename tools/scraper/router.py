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

# A single router log file may contain data from multiple instances of
# that router booting and running. Thus there may be several different
# connections labeled [0] and these connections may be to different
# routers on each run.
#
# The 'router' class defined here represents a single boot-and-run
# instance from the log file.
#

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

import sys
import traceback
import datetime

import amqp_detail
import common
import text


class RestartRecord():

    def __init__(self, _router, _line, _lineno):
        self.router = _router
        self.line = _line
        self.lineno = _lineno
        try:
            self.datetime = datetime.datetime.strptime(self.line[:26], '%Y-%m-%d %H:%M:%S.%f')
        except:
            self.datetime = datetime.datetime(1970, 1, 1)

    def __repr__(self):
        return "%d instance %d start %s #%d" % (self.router.log_index, self.router.instance,
                                                self.datetime, self.lineno)


class Router():
    """A single dispatch boot-and-run instance from a log file"""

    def __init__(self, _fn, _log_index, _instance):
        self.fn = _fn                 # log file name
        self.log_index = _log_index   # 0=A, 1=B, ...
        self.instance = _instance     # log file instance of router
        self.iletter = common.log_letter_of(self.log_index)  # A
        self.iname = self.iletter + str(self.instance)       # A0

        # discovered Container Name
        self.container_name = None

        # discovered Version
        self.version = None

        # discovered mode
        self.mode = None

        # restart_rec - when this router was identified in log file
        self.restart_rec = None

        # lines - the log lines as ParsedLogLine objects
        self.lines = []

        # conn_list - List of connections discovered in log lines
        # Sorted in ascending order and not necessarily in packed sequence.
        self.conn_list = []

        # conn_log_lines - count of log lines per connection
        self.conn_log_lines = {}

        # conn_transfer_bytes - count of bytes transfered over this connection
        self.conn_xfer_bytes = {}

        # connection_to_frame_map
        self.conn_to_frame_map = {}

        # conn_peer - peer container long name
        #   key= connection id '1', '2'
        #   val= original peer container name
        self.conn_peer = {}

        # conn_peer_display - peer container display name
        #   key= connection id '1', '2'
        #   val= display name
        # Peer display name shortened with popup if necessary
        self.conn_peer_display = {}

        # conn_peer_connid - display value for peer's connection id
        #   key= connection id '1', '2'
        #   val= peer's connid 'A.0_3', 'D.3_18'
        self.conn_peer_connid = {}

        # conn_dir - arrow indicating connection origin direction
        #   key= connection id '1', '2'
        #   val= '<-' peer created conn, '->' router created conn
        self.conn_dir = {}

        # router_ls - link state 'ROUTER_LS (info)' lines
        self.router_ls = []

        # open and close times
        self.conn_open_time = {}   # first log line with [N] seen
        self.conn_close_time = {}  # last close log line seen

        # details: for each connection, for each session, for each link, whaaa?
        self.details = None

    def discover_connection_facts(self, comn):
        """
        Discover all the connections in this router-instance log
        For each connection:
         * determine connection direction
         * discover name of peer container
         * generate html to use to display the peer nickname
         * count log lines
         * count transfer bytes
        :param comn:
        :return:
        """
        for item in self.lines:
            if item.data.is_scraper:
                # scraper lines are pass-through
                continue
            conn_num = int(item.data.conn_num)
            id = item.data.conn_id           # full name A0_3
            if conn_num not in self.conn_list:
                cdir = ""
                if item.data.direction != "":
                    cdir = item.data.direction
                else:
                    if "Connecting" in item.data.web_show_str:
                        cdir = text.direction_out()
                    elif "Accepting" in item.data.web_show_str:
                        cdir = text.direction_in()
                self.conn_list.append(conn_num)
                self.conn_to_frame_map[id] = []
                self.conn_dir[id] = cdir
                self.conn_log_lines[id] = 0   # line counter
                self.conn_xfer_bytes[id] = 0  # byte counter
                self.conn_open_time[id] = item
            self.conn_to_frame_map[id].append(item)
            # inbound open handling
            if item.data.name == "open" and item.data.direction == text.direction_in():
                if item.data.conn_id in self.conn_peer:
                    sys.exit('ERROR: file: %s connection %s has multiple connection peers' % (
                        self.fn, id))
                self.conn_peer[id] = item.data.conn_peer
                self.conn_peer_display[id] = comn.shorteners.short_peer_names.translate(
                    item.data.conn_peer, True)
            # close monitor
            if item.data.name == "close":
                self.conn_close_time[id] = item
            # connection log-line count
            self.conn_log_lines[id] += 1
            # transfer byte count
            if item.data.name == "transfer":
                self.conn_xfer_bytes[id] += int(item.data.transfer_size)
        self.conn_list = sorted(self.conn_list)
        self.details = amqp_detail.AllDetails(self, comn)

    def conn_id(self, conn_num):
        """
        Given this router's connection number return the global connection id
        :param conn_num: connection number
        :return: conn_id in the for A0_3
        """
        return self.iname + "_" + str(conn_num)

    def is_interior(self):
        return self.mode == "interior"


def which_router_tod(router_list, at_time):
    """
    Find a router in a list based on time of day
    :param router_list: a list of Router objects
    :param at_time: the datetime record identifying the router
    :return: tuple: (a router from the list or None, router index)
    """
    if len(router_list) == 0:
        return (None, 0)
    if len(router_list) == 1:
        return (router_list[0], 0)
    for i in range(1, len(router_list)):
        if at_time < router_list[i].restart_rec.datetime:
            return (router_list[i - 1], i - 1)
    return (router_list[-1], len(router_list) - 1)


def which_router_id_tod(routers, id, at_time):
    """
    Find a router by container_name and time of day
    :param routers: a list of router instance lists
    :param id: the container name
    :param at_time: datetime of interest
    :return: the router that had that container name at that time; None if not found
    """
    for routerlist in routers:
        if routerlist[0].container_name == id:
            return which_router_tod(routerlist, at_time)
    return (None, 0)


if __name__ == "__main__":
    try:
        pass
    except:
        traceback.print_exc(file=sys.stdout)
        pass
