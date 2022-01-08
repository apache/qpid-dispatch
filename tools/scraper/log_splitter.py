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

# Split a gigantic (or not) log file into files of traffic for each connection.
# Identify probable router and broker connections, QpidJMS client connections,
# and AMQP errors. Create lists of connections sorted by log line and by transfer counts.
# Emit a web page summarizing the results.

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

import os
import sys
import traceback
from collections import defaultdict
from datetime import datetime

import common
import text


class connection():

    def __init__(self, instance, conn_id, logfile):
        self.instance = instance
        self.conn_id = conn_id
        self.logfile = logfile
        self.lines = []
        self.attaches = []
        self.key_name = connection.keyname(instance, conn_id)
        self.transfers = 0
        self.peer_open = ""
        self.peer_type = ""
        self.log_n_lines = 0
        self.log_n_dir = ""
        self.file_name = ""
        self.path_name = ""

    @staticmethod
    def keyname(instance, conn_id):
        tmp = "0000000" + str(conn_id)
        return str(instance) + "." + tmp[-8:]

    def disp_name(self):
        return str(self.instance) + "_" + str(self.conn_id)

    def generate_paths(self):
        self.log_n_dir = "10e%d" % self.log_n_lines
        self.file_name = self.disp_name() + ".log"
        self.path_name = self.log_n_dir + "/" + self.file_name


class parsed_attach():
    """
    Parse an attach log line. The usual parser is way too slow
    so this does the essentials for --split.
    """

    def find_field(self, key, line):
        sti = line.find(key)
        if sti < 0:
            return 'none'
        ste = line.find(',', sti + len(key))
        if ste < 0:
            raise ValueError("Value not properly delimited. Key '%s'. line: %s" % (key, self.line))
        val = line[sti + len(key):ste]
        if val.startswith('"'):
            val = val[1:-1]
        return val

    def __init__(self, instance, line, opaque):
        self.instance = instance
        self.line = line
        self.opaque = opaque
        self.datetime = None
        self.conn_num = ""
        self.conn_id = ""
        self.direction = ""
        self.role = ""
        self.source = ""
        self.target = ""

        # timestamp
        try:
            self.datetime = datetime.strptime(self.line[:26], '%Y-%m-%d %H:%M:%S.%f')
        except:
            # old routers flub the timestamp and don't print leading zero in uS time
            # 2018-11-18 11:31:08.269 should be 2018-11-18 11:31:08.000269
            td = self.line[:26]
            parts = td.split('.')
            us = parts[1]
            parts_us = us.split(' ')
            if len(parts_us[0]) < 6:
                parts_us[0] = '0' * (6 - len(parts_us[0])) + parts_us[0]
            parts[1] = ' '.join(parts_us)
            td = '.'.join(parts)
            try:
                self.datetime = datetime.strptime(td[:26], '%Y-%m-%d %H:%M:%S.%f')
            except:
                self.datetime = datetime(1970, 1, 1)
        key_strace = "SERVER (trace) ["
        sti = self.line.find(key_strace)
        if sti < 0:
            key_strace = "PROTOCOL (trace) ["
            sti = self.line.find(key_strace)
            if sti < 0:
                raise ValueError("'%s' not found in line %s" % (key_strace, self.line))
        self.line = self.line[sti + len(key_strace):]
        ste = self.line.find(']')
        if ste < 0:
            print("Failed to parse line ", self.line)
            raise ValueError("'%s' not found in line %s" % ("]", self.line))
        self.conn_num = self.line[:ste]
        self.line = self.line[ste + 1:]
        self.conn_id = "A" + str(self.instance) + "_" + str(self.conn_num)
        # get the session (channel) number
        if self.line.startswith(':'):
            self.line = self.line[1:]
        proton_frame_key = "FRAME: "
        if self.line.startswith(proton_frame_key):
            self.line = self.line[len(proton_frame_key):]

        sti = self.line.find(' ')
        if sti < 0:
            raise ValueError("space not found after channel number at head of line %s" % (self.line))
        if sti > 0:
            self.channel = self.line[:sti]
        self.line = self.line[sti + 1:]
        self.line = self.line.lstrip()
        # direction
        if self.line.startswith('<') or self.line.startswith('-'):
            self.direction = self.line[:2]
            self.line = self.line[3:]
        else:
            raise ValueError("line does not have direction arrow: %s" % (self.line))
        self.role = "receiver" if self.find_field('role=', self.line) == "true" else "sender"
        self.source = self.find_field('@source(40) [address=', self.line)
        self.target = self.find_field('@target(41) [address=', self.line)


class LogFile:

    def __init__(self, fn, top_n=24):
        """
        Represent connections in a file
        :param fn: file name
        :param
        """
        self.log_fn = fn    # file name
        self.top_n = top_n  # how many to report
        self.instance = 0   # incremented when router restarts in log file
        self.amqp_lines = 0  # server trace lines
        self.transfers = 0  # server transfers
        self.attaches = 0   # server attach

        # restarts
        self.restarts = []

        # connections
        # dictionary of connection data
        # key = connection id: <instance>.<conn_id>    "0.3"
        # val = connection class object
        self.connections = {}

        # router_connections
        # list of received opens that suggest a router at the other end
        self.router_connections = []

        # broker connections
        # list of received opens that suggest a broker at the other end
        self.broker_connections = []

        # errors
        # amqp errors in time order
        self.errors = []

        # conns_by_size_transfer
        # all connections in transfer size descending order
        self.conns_by_size_transfer = []

        # conns_by_size_loglines
        # all connections in log_lines size descending order
        self.conns_by_size_loglines = []

        # histogram - count of connections with N logs < 10^index
        # [0] = N < 10^0
        # [1] = N < 10^1
        self.histogram = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        self.hist_max = len(self.histogram) - 1

    def parse_identify(self, text, line, before_col=70):
        """
        Look for text in line but make sure it's not in the body of some message,
        :param text:
        :param line:
        :param before_col: limit on how far to search into line
        """
        st = line.find(text, 0, (before_col + len(text)))
        if st < 0:
            return False
        return st < 70

    def parse_line(self, line):
        """
        Do minimum parsing on line.
        If container name then bump instance value
        If server trace then get conn_id and add line to connections data
        :param line:
        :return:
        """
        key_sstart = "SERVER (info) Container Name:"  # Normal 'router is starting' restart discovery line
        key_strace = "SERVER (trace) ["  # AMQP traffic
        key_ptrace = "PROTOCOL (trace) ["  # AMQP traffic
        key_error = "@error(29)"
        key_openin = "<- @open(16)"
        key_xfer = "@transfer"
        key_attach = "@attach"
        key_prod_dispatch = ':product="qpid-dispatch-router"'
        key_prod_aartemis = ':product="apache-activemq-artemis"'
        key_prod_aqpidcpp = ':product="qpid-cpp"'
        key_prod_aqpidjms = ':product="QpidJMS"'

        if self.parse_identify(key_sstart, line):
            self.instance += 1
            self.restarts.append(line)
        else:
            found = False
            if self.parse_identify(key_strace, line):
                self.amqp_lines += 1
                idx = line.find(key_strace)
                idx += len(key_strace)
                found = True
            elif self.parse_identify(key_ptrace, line):
                self.amqp_lines += 1
                idx = line.find(key_ptrace)
                idx += len(key_ptrace)
                found = True
            if found:
                eidx = line.find("]", idx + 1)
                conn_id = line[idx:eidx]
                keyname = connection.keyname(self.instance, conn_id)
                if keyname not in self.connections:
                    self.connections[keyname] = connection(self.instance, conn_id, self)
                curr_conn = self.connections[keyname]
                curr_conn.lines.append(line)
                # router hint
                if key_openin in line:
                    # inbound open
                    if key_prod_dispatch in line:
                        self.router_connections.append(curr_conn)
                        curr_conn.peer_open = line
                        curr_conn.peer_type = key_prod_dispatch
                    elif key_prod_aqpidjms in line:
                        curr_conn.peer_type = key_prod_aqpidjms
                    else:
                        for k in [key_prod_aartemis, key_prod_aqpidcpp]:
                            if k in line:
                                self.broker_connections.append(curr_conn)
                                curr_conn.peer_open = line
                                curr_conn.peer_type = k
                elif key_attach in line:
                    self.attaches += 1
                    curr_conn.attaches.append(line)
                elif self.parse_identify(key_xfer, line):
                    self.transfers += 1
                    curr_conn.transfers += 1
        if key_error in line:
            self.errors.append(line)

    def log_of(self, x):
        """
        calculate nearest power of 10 > x
        :param x:
        :return:
        """
        for i in range(self.hist_max):
            if x < 10 ** i:
                return i
        return self.hist_max

    def sort_sizes(self, sortfunc1, sortfunc2):
        smap = defaultdict(list)
        conns_by_size = []
        # create size map. index is size, list holds all connections of that many transfers
        for k, v in self.connections.items():
            smap[str(sortfunc1(v))].append(v)
        # create a sorted list of sizes in sizemap
        sl = list(smap.keys())
        sli = [int(k) for k in sl]
        slist = sorted(sli, reverse=True)
        # create grand list of all connections
        for cursize in slist:
            lsm = smap[str(cursize)]
            lsm = sorted(lsm, key=sortfunc2, reverse=True)
            #lsm = sorted(lsm, key = lambda x: int(x.conn_id))
            for ls in lsm:
                conns_by_size.append(ls)
        return conns_by_size

    def summarize_connections(self):
        # sort connections based on transfer count and on n log lines
        self.conns_by_size_transfer = self.sort_sizes(lambda x: x.transfers, lambda x: len(x.lines))
        self.conns_by_size_loglines = self.sort_sizes(lambda x: len(x.lines), lambda x: x.transfers)

        # compute log_n and file name facts for all connections
        for k, v in self.connections.items():
            v.log_n_lines = self.log_of(len(v.lines))
            v.generate_paths()

        # Write the web doc to stdout
        print("""<!DOCTYPE html>
<html>
<head>
<title>%s qpid-dispatch log split</title>

<style>
    * {
    font-family: sans-serif;
}
table {
    border-collapse: collapse;
}
table, td, th {
    border: 1px solid black;
    padding: 3px;
}
</style>
<script src="http://ajax.googleapis.com/ajax/libs/dojo/1.4/dojo/dojo.xd.js" type="text/javascript"></script>
<!-- <script src="http://ajax.googleapis.com/ajax/libs/dojo/1.4/dojo/dojo.xd.js" type="text/javascript"></script> -->
<script type="text/javascript">
function node_is_visible(node)
{
  if(dojo.isString(node))
    node = dojo.byId(node);
  if(!node)
    return false;
  return node.style.display == "block";
}
function set_node(node, str)
{
  if(dojo.isString(node))
    node = dojo.byId(node);
  if(!node) return;
  node.style.display = str;
}
function toggle_node(node)
{
  if(dojo.isString(node))
    node = dojo.byId(node);
  if(!node) return;
  set_node(node, (node_is_visible(node)) ? 'none' : 'block');
}
function hide_node(node)
{
  set_node(node, 'none');
}
function show_node(node)
{
  set_node(node, 'block');
}

""" % self.log_fn)

        print("</script>")
        print("</head>")
        print("<body>")
        print("""
<h3>Contents</h3>
<table>
<tr> <th>Section</th>                                                     <th>Description</th> </tr>
<tr><td><a href=\"#c_summary\"        >Summary</a></td>                   <td>Summary</td></tr>
<tr><td><a href=\"#c_restarts\"       >Router restarts</a></td>           <td>Router reboot records</td></tr>
<tr><td><a href=\"#c_router_conn\"    >Interrouter connections</a></td>   <td>Probable interrouter connections</td></tr>
<tr><td><a href=\"#c_broker_conn\"    >Broker connections</a></td>        <td>Probable broker connections</td></tr>
<tr><td><a href=\"#c_errors\"         >AMQP errors</a></td>               <td>AMQP errors</td></tr>
<tr><td><a href=\"#c_conn_xfersize\"  >Conn by N transfers</a></td>       <td>Connections sorted by transfer log count</td></tr>
<tr><td><a href=\"#c_conn_xfer0\"     >Conn with no transfers</a></td>    <td>Connections with no transfers</td></tr>
<tr><td><a href=\"#c_conn_logsize\"   >Conn by N log lines</a></td>       <td>Connections sorted by total log line count</td></tr>
<tr><td><a href=\"#c_addresses\"      >Addresses</a></td>                 <td>AMQP address usage</td></tr>
</table>
<hr>
""")
        print("<a name=\"c_summary\"></a>")
        print("<table>")
        print("<tr><th>Statistic</th>          <th>Value</th></tr>")
        print("<tr><td>File</td>               <td>%s</td></tr>" % self.log_fn)
        print("<tr><td>Router starts</td>      <td>%s</td></tr>" % str(self.instance))
        print("<tr><td>Connections</td>        <td>%s</td></tr>" % str(len(self.connections)))
        print("<tr><td>Router connections</td> <td>%s</td></tr>" % str(len(self.router_connections)))
        print("<tr><td>AMQP log lines</td>     <td>%s</td></tr>" % str(self.amqp_lines))
        print("<tr><td>AMQP errors</td>        <td>%s</td></tr>" % str(len(self.errors)))
        print("<tr><td>AMQP transfers</td>     <td>%s</td></tr>" % str(self.transfers))
        print("<tr><td>AMQP attaches</td>     <td>%s</td></tr>" % str(self.attaches))
        print("</table>")
        print("<hr>")

        # Restarts
        print("<a name=\"c_restarts\"></a>")
        print("<h3>Restarts</h3>")
        for i in range(1, (self.instance + 1)):
            rr = self.restarts[i - 1]
            print("(%d) - %s<br>" % (i, rr), end='')
        print("<hr>")

        # interrouter connections
        print("<a name=\"c_router_conn\"></a>")
        print("<h3>Probable inter-router connections (N=%d)</h3>" % (len(self.router_connections)))
        print("<table>")
        print("<tr><th>Connection</th> <th>Transfers</th> <th>Log lines</th> <th>AMQP Open<th></tr>")
        for rc in self.router_connections:
            print("<tr><td><a href=\"%s/%s\">%s</a></td><td>%d</td><td>%d</td><td>%s</td></tr>" %
                  (rc.logfile.odir(), rc.path_name, rc.disp_name(), rc.transfers, len(rc.lines),
                   common.html_escape(rc.peer_open)))
        print("</table>")
        print("<hr>")

        # broker connections
        print("<a name=\"c_broker_conn\"></a>")
        print("<h3>Probable broker connections (N=%d)</h3>" % (len(self.broker_connections)))
        print("<table>")
        print("<tr><th>Connection</th> <th>Transfers</th> <th>Log lines</th> <th>AMQP Open<th></tr>")
        for rc in self.broker_connections:
            print("<tr><td><a href=\"%s/%s\">%s</a></td><td>%d</td><td>%d</td><td>%s</td></tr>" %
                  (rc.logfile.odir(), rc.path_name, rc.disp_name(), rc.transfers, len(rc.lines),
                   common.html_escape(rc.peer_open)))
        print("</table>")
        print("<hr>")

        # histogram
        # for cursize in self.sizelist:
        #    self.histogram[self.log_of(cursize)] += len(self.sizemap[str(cursize)])
        # print()
        #print("Log lines per connection distribution")
        # for i in range(1, self.hist_max):
        #    print("N <  10e%d : %d" %(i, self.histogram[i]))
        #print("N >= 10e%d : %d" % ((self.hist_max - 1), self.histogram[self.hist_max]))

        # errors
        print("<a name=\"c_errors\"></a>")
        print("<h3>AMQP errors (N=%d)</h3>" % (len(self.errors)))
        print("<table>")
        print("<tr><th>N</th> <th>AMQP error</th></tr>")
        for i in range(len(self.errors)):
            print("<tr><td>%d</td> <td>%s</td></tr>" % (i, common.html_escape(self.errors[i].strip())))
        print("</table>")
        print("<hr>")

    def odir(self):
        return os.path.join(os.getcwd(), (self.log_fn + ".splits"))

    def write_subfiles(self):
        # Q: Where to put the generated files? A: odir
        odir = self.odir()
        odirs = ['dummy']  # dirs indexed by log of n-lines

        os.makedirs(odir)
        for i in range(1, self.hist_max):
            nrange = ("10e%d" % (i))
            ndir = os.path.join(odir, nrange)
            os.makedirs(ndir)
            odirs.append(ndir)

        for k, c in self.connections.items():
            cdir = odirs[self.log_of(len(c.lines))]
            opath = os.path.join(cdir, (c.disp_name() + ".log"))
            with open(opath, 'w') as f:
                for l in c.lines:
                    f.write(l)

        xfer0 = 0
        for rc in self.conns_by_size_transfer:
            if rc.transfers == 0:
                xfer0 += 1
        print("<a name=\"c_conn_xfersize\"></a>")
        print("<h3>Connections by transfer count (N=%d)</h3>" % (len(self.conns_by_size_transfer) - xfer0))
        print("<table>")
        n = 1
        print("<tr><th>N</th><th>Connection</th> <th>Transfers</th> <th>Log lines</th> <th>Type</th> <th>AMQP detail<th></tr>")
        for rc in self.conns_by_size_transfer:
            if rc.transfers > 0:
                print("<tr><td>%d</td><td><a href=\"%s/%s\">%s</a></td> <td>%d</td> <td>%d</td> <td>%s</td> <td>%s</td></tr>" %
                      (n, rc.logfile.odir(), rc.path_name, rc.disp_name(), rc.transfers, len(rc.lines),
                       rc.peer_type, common.html_escape(rc.peer_open)))
                n += 1
        print("</table>")
        print("<hr>")

        print("<a name=\"c_conn_xfer0\"></a>")
        print("<h3>Connections with no AMQP transfers (N=%d)</h3>" % (xfer0))
        print("<table>")
        n = 1
        print("<tr><th>N</th><th>Connection</th> <th>Transfers</th> <th>Log lines</th> <th>Type</th> <th>AMQP detail<th></tr>")
        for rc in self.conns_by_size_transfer:
            if rc.transfers == 0:
                print("<tr><td>%d</td><td><a href=\"%s/%s\">%s</a></td> <td>%d</td> <td>%d</td> <td>%s</td> <td>%s</td></tr>" %
                      (n, rc.logfile.odir(), rc.path_name, rc.disp_name(), rc.transfers, len(rc.lines),
                       rc.peer_type, common.html_escape(rc.peer_open)))
                n += 1
        print("</table>")
        print("<hr>")

        print("<a name=\"c_conn_logsize\"></a>")
        print("<h3>Connections by total log line count (N=%d)</h3>" % (len(self.conns_by_size_loglines)))
        print("<table>")
        n = 1
        print("<tr><th>N</th><th>Connection</th> <th>Transfers</th> <th>Log lines</th> <th>Type</th> <th>AMQP detail<th></tr>")
        for rc in self.conns_by_size_loglines:
            print("<tr><td>%d</td><td><a href=\"%s/%s\">%s</a></td> <td>%d</td> <td>%d</td> <td>%s</td> <td>%s</td></tr>" %
                  (n, rc.logfile.odir(), rc.path_name, rc.disp_name(), rc.transfers, len(rc.lines),
                   rc.peer_type, common.html_escape(rc.peer_open)))
            n += 1
        print("</table>")
        print("<hr>")

    def aggregate_addresses(self):
        class dummy_args():
            skip_all_data = False
            skip_detail = False
            skip_msg_progress = False
            split = False
            time_start = None
            time_end = None

        comn = common.Common()
        comn.args = dummy_args

        print("<a name=\"c_addresses\"></a>")

        # Aggregate link source/target addresses where the names are referenced in the attach:
        #  observe source and target addresses regardless of the role of the link
        # TODO speed this up a little
        nn2 = defaultdict(list)
        for k, conn in self.connections.items():
            for aline in conn.attaches:
                try:
                    pl = parsed_attach(conn.instance, aline, k)
                except Exception as e:
                    # t, v, tb = sys.exc_info()
                    if hasattr(e, 'message'):
                        sys.stderr.write("Failed to parse %s. Analysis continuing...\n" % (e.message))
                    else:
                        sys.stderr.write("Failed to parse %s. Analysis continuing...\n" % (e))
                if pl is not None:
                    nn2[pl.source].append(pl)
                    if pl.source != pl.target:
                        nn2[pl.target].append(pl)

        print("<h3>Verbose AMQP Addresses Overview (N=%d)</h3>" % len(nn2))
        addr_many = []
        addr_few = []
        ADDR_LEVEL = 4
        n = 0
        for k in sorted(nn2.keys()):
            plfs = nn2[k]
            showthis = ("<a href=\"javascript:toggle_node('@@addr2_%d')\">%s</a>" %
                        (n, text.lozenge()))
            visitthis = ("<a href=\"#@@addr2_%d_data\">%s</a>" %
                         (n, k))
            line = ("<tr><td>%s %s</td> <td>%d</td> </tr>" %
                    (showthis, visitthis, len(plfs)))
            if len(plfs) <= ADDR_LEVEL:
                addr_few.append(line)
            else:
                addr_many.append(line)
            n += 1
        showthis = ("<a href=\"javascript:toggle_node('addr_table_many')\">%s</a>" %
                    (text.lozenge()))
        print(" %s Addresses attached more than %d times (N=%d) <br>" % (showthis, ADDR_LEVEL, len(addr_many)))
        print("<div id=\"addr_table_many\" style=\"display:none; margin-top: 2px; margin-bottom: 2px; margin-left: 10px\">")
        print("<h4>Addresses with many links (N=%d)</h4>" % (len(addr_many)))
        print("<table><tr> <th>Address</th> <th>N References</th> </tr>")
        for line in addr_many:
            print(line)
        print("</table>")
        print("</div>")

        showthis = ("<a href=\"javascript:toggle_node('addr_table_few')\">%s</a>" %
                    (text.lozenge()))
        print(" %s Addresses attached %d times or fewer (N=%d)<br>" % (showthis, ADDR_LEVEL, len(addr_few)))
        print("<div id=\"addr_table_few\" style=\"display:none; margin-top: 2px; margin-bottom: 2px; margin-left: 10px\">")
        print("<h4>Addresses with few links (N=%d)</h4>" % (len(addr_few)))
        print("<table><tr> <th>Address</th> <th>N References</th> </tr>")
        for line in addr_few:
            print(line)
        print("</table>")
        print("</div>")

        # loop to print expandable sub tables
        print("<h3>AMQP Addresses Details</h3>")
        n = 0
        for k in sorted(nn2.keys()):
            plfss = nn2[k]
            plfs = sorted(plfss, key=lambda lfl: lfl.datetime)
            print("<div id=\"@@addr2_%d\" style=\"display:none; margin-top: 2px; margin-bottom: 2px; margin-left: 10px\">" %
                  (n))
            print("<a name=\"@@addr2_%d_data\"></a>" % (n))
            print("<h4>Address %s</h4>" % (k))
            print("<table><tr><th>Time</th> <th>Connection</th> <th>Dir</th> <th>Peer</th> <th>Role</th> <th>Source</th> <th>Target</th> </tr>")
            for plf in plfs:
                print("<tr><td>%s</td> <td>%s</td> <td>%s</td> <td>%s</td> <td>%s</td> <td>%s</td> <td>%s</td> </tr>" %
                      (plf.datetime, plf.conn_id,
                       plf.direction, self.connections[plf.opaque].peer_type,
                       plf.role, plf.source, plf.target))
            print("</table>")
            print("</div>")
            n += 1


#
#
def main_except(log_fn):
    """Given a log file name, split the file into per-connection sub files"""
    log_files = []

    if not os.path.exists(log_fn):
        sys.exit('ERROR: log file %s was not found!' % log_fn)

    # parse the log file
    with open(log_fn, 'r') as infile:
        lf = LogFile(log_fn)
        odir = lf.odir()
        if os.path.exists(odir):
            sys.exit('ERROR: output directory %s exists' % odir)
        log_files.append(lf)
        for line in infile:
            lf.parse_line(line)

    # write output
    for lf in log_files:
        lf.summarize_connections()  # prints web page to console
        lf.write_subfiles()        # generates split files one-per-connection
        lf.aggregate_addresses()   # print address table html to console

    # close the doc
    print("</body>")


def main(argv):
    try:
        if len(argv) != 2:
            sys.exit('Usage: %s log-file-name' % argv[0])
        main_except(argv[1])
        return 0
    except Exception as e:
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
