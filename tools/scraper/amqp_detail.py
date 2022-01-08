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

import datetime
import sys
import traceback

import common
import text

"""
Given a map of all connections with lists of the associated frames
analyze and show per-connection, per-session, and per-link details.

This is done in a two-step process:
 * Run through the frame lists and generates an intermediate structure
   with the the details for display.
 * Generate the html from the detail structure.
This strategy allows for a third step that would allow more details
to be gleaned from the static details. For instance, if router A
sends a transfer to router B then router A's details could show
how long it took for the transfer to reach router B. Similarly
router B's details could show how long ago router A sent the transfer.
"""


class Counts():
    """
    Holds common count sets that can be rolled up from links to
    sessions to connections. Not for individual performatives.
    """

    def __init__(self):
        # amqp errors gleaned from any performative
        self.errors = 0    # amqp error - simple count
        # derived facts about message settlement
        self.unsettled = 0
        self.presettled = 0
        self.accepted = 0
        self.rejected = 0
        self.released = 0
        self.modified = 0
        # interesting transfers
        self.aborted = 0
        self.more = 0
        self.incomplete = 0
        # link drained
        self.drain = 0
        # link out of credit
        self.credit_not_evaluated = 0
        self.no_credit = 0  # event count, excludes drain credit exhaustion
        self.initial_no_credit_duration = datetime.timedelta()  # before first credit
        self.no_credit_duration = datetime.timedelta()  # after credit issued and then exhausted

    def highlight(self, name, value, color):
        """
        if value is non zero then return a colorized 'name: value' text stream
        else return a blank string
        """
        result = ""
        if value:
            result = "<span style=\"background-color:%s\">%s: %s</span> " % (color, name, str(value))
        return result

    def highlight_duration(self, name, value, color):
        """
        if value is non zero then return a colorized 'name: value' text stream
        else return a blank string
        """
        result = ""
        if value.seconds > 0 or value.microseconds > 0:
            t = float(value.seconds) + float(value.microseconds) / 1000000.0
            result = "<span style=\"background-color:%s\">%s: %0.06f</span> " % (color, name, t)
        return result

    def show_html(self):
        res = ""
        res += self.highlight("errors", self.errors, common.color_of("errors"))
        res += self.highlight("unsettled", self.unsettled, common.color_of("unsettled"))
        res += self.highlight("presettled", self.presettled, common.color_of("presettled"))
        res += self.highlight("accepted", self.accepted, common.color_of("accepted"))
        res += self.highlight("rejected", self.rejected, common.color_of("rejected"))
        res += self.highlight("released", self.released, common.color_of("released"))
        res += self.highlight("modified", self.modified, common.color_of("modified"))
        res += self.highlight("aborted", self.aborted, common.color_of("aborted"))
        res += self.highlight("more", self.more, common.color_of("more"))
        res += self.highlight("incomplete", self.incomplete, common.color_of("unsettled"))
        res += self.highlight("drain", self.drain, common.color_of("drain"))
        res += self.highlight_duration("initial", self.initial_no_credit_duration, common.color_of("no_credit"))
        res += self.highlight("no_credit", self.no_credit, common.color_of("no_credit"))
        res += self.highlight_duration("duration", self.no_credit_duration, common.color_of("no_credit"))
        res += self.highlight("no_eval", self.credit_not_evaluated, common.color_of("no_credit"))
        return res

    @classmethod
    def show_table_heads1(cls):
        return "<th rowspan=\"2\"><span title=\"AMQP errors\">ERR</span></th>" \
               "<th colspan=\"6\">Settlement - disposition</th>" \
               "<th colspan=\"3\">Transfer</th>" \
               "<th>Flow</th>" \
               "<th colspan=\"4\">Credit starvation</th>"

    @classmethod
    def show_table_heads2(cls):
        return "<th><span title=\"Unsettled transfers\">UNSTL</span></th>" \
               "<th><span title=\"Presettled transfers\">PRE</span></th>" \
               "<th><span title=\"Disposition: accepted\">ACCPT</span></th>" \
               "<th><span title=\"Disposition: rejected\">RJCT</span></th>" \
               "<th><span title=\"Disposition: released\">RLSD</span></th>" \
               "<th><span title=\"Disposition: modified\">MDFD</span></th>" \
               "<th><span title=\"Transfer abort=true\">ABRT</span></th>" \
               "<th><span title=\"Transfer: more=true\">MOR</span></th>" \
               "<th><span title=\"Transfer: incomplete; all frames had more=true\">INC</span></th>" \
               "<th><span title=\"Flow: drain=true\">DRN</span></th>" \
               "<th><span title=\"Initial stall (S)\">initial (S)</span></th>" \
               "<th><span title=\"Credit exhausted\">-> 0</span></th>" \
               "<th><span title=\"Normal credit exhaustion stall (S)\">duration (S)</span></th>" \
               "<th><span title=\"Credit not evaluated\">?</span></th>"

    def show_table_element(self, name, value, color):
        return ("<td>%s</td>" % text.nbsp()) if value == 0 else \
            ("<td>%s</td>" % ("<span style=\"background-color:%s\">%s</span> " % (color, str(value))))

    def show_table_duration(self, delta):
        if delta.seconds == 0 and delta.microseconds == 0:
            return "<td>%s</td>" % text.nbsp()
        t = float(delta.seconds) + float(delta.microseconds) / 1000000.0
        return ("<td>%0.06f</td>" % t)

    def show_table_data(self):
        res = ""
        res += self.show_table_element("errors", self.errors, common.color_of("errors"))
        res += self.show_table_element("unsettled", self.unsettled, common.color_of("unsettled"))
        res += self.show_table_element("presettled", self.presettled, common.color_of("presettled"))
        res += self.show_table_element("accepted", self.accepted, common.color_of("accepted"))
        res += self.show_table_element("rejected", self.rejected, common.color_of("rejected"))
        res += self.show_table_element("released", self.released, common.color_of("released"))
        res += self.show_table_element("modified", self.modified, common.color_of("modified"))
        res += self.show_table_element("aborted", self.aborted, common.color_of("aborted"))
        res += self.show_table_element("more", self.more, common.color_of("more"))
        res += self.show_table_element("incomplete", self.incomplete, common.color_of("unsettled"))
        res += self.show_table_element("drain", self.drain, common.color_of("drain"))
        res += self.show_table_duration(self.initial_no_credit_duration)
        res += self.show_table_element("no_credit", self.no_credit, common.color_of("no_credit"))
        res += self.show_table_duration(self.no_credit_duration)
        res += self.show_table_element("?", self.credit_not_evaluated, common.color_of("no_credit"))
        return res


class ConnectionDetail():
    """Holds facts about sessions over the connection's lifetime"""

    def __init__(self, id, router, conn):
        # id in form 'A_15':
        #   A is the router logfile key
        #   15 is the log connection number [15]
        self.id = id
        self.router = router
        self.conn = conn     # from router.conn_list

        # seq_no number differentiates items that otherwise have same identifiers.
        # Sessions, for example: a given connection may have N distinct session
        # with local channel 0.
        self.seq_no = 0

        # combined counts
        self.counts = Counts()

        # session_list holds all SessionDetail records either active or retired
        # Sessions for a connection are identified by the local channel number.
        # There may be many sessions all using the same channel number.
        # This list holds all of them.
        self.session_list = []

        # this map indexed by the channel refers to the current item in the session_list
        self.chan_map = {}

        # count of AMQP performatives for this connection that are not accounted
        # properly in session and link processing.
        # Server Accepting, SASL mechs, init, outcome, AMQP, and so on
        self.unaccounted_frame_list = []

    def FindSession(self, channel):
        """
        Find the current session by channel number
        :param channel: the performative channel
        :return: the session or None
        """
        return self.chan_map[channel] if channel in self.chan_map else None

    def GetId(self):
        return self.id

    def GetSeqNo(self):
        self.seq_no += 1
        return str(self.seq_no)

    def EndChannel(self, channel):
        # take existing session out of connection chan map
        if channel in self.chan_map:
            del self.chan_map[channel]

    def GetLinkEventCount(self):
        c = 0
        for session in self.session_list:
            c += session.GetLinkEventCount()
        return c


class SessionDetail:
    """Holds facts about a session"""

    def __init__(self, id, conn_detail, conn_seq, start_time):
        # parent connection
        self.id = id
        self.conn_detail = conn_detail

        # some seq number
        self.conn_epoch = conn_seq

        # Timing
        self.time_start = start_time
        self.time_end = start_time

        # combined counts
        self.counts = Counts()

        self.channel = -1
        self.peer_chan = -1

        self.half_closed = False

        self.direction = ""

        # seq_no number differentiates items that otherwise have same identifiers.
        # links for example
        self.seq_no = 0

        self.log_line_list = []

        # link_list holds LinkDetail records
        # Links for a session are identified by a (handle, remote-handle) number pair.
        # There may be many links all using the same handle pairs.
        # This list holds all of them.
        self.link_list = []

        # link_list holds all links either active or retired
        # this map indexed by the handle refers to the current item in the link_list
        self.input_handle_link_map = {}  # link created by peer
        self.output_handle_link_map = {}  # link created locally

        # Link name in attach finds link details in link_list
        # This map contains the link handle to disambiguate the name
        self.link_name_to_detail_map = {}
        #
        # The map contains the pure link name and is used only to resolve name collisions
        self.link_name_conflict_map = {}

        # count of AMQP performatives for this connection that are not accounted
        # properly in link processing
        self.session_frame_list = []

        # Session dispositions
        # Sender/receiver dispositions may be sent or received
        self.rx_rcvr_disposition_map = {}  # key=delivery id, val=disposition plf
        self.rx_sndr_disposition_map = {}  # key=delivery id, val=disposition plf
        self.tx_rcvr_disposition_map = {}  # key=delivery id, val=disposition plf
        self.tx_sndr_disposition_map = {}  # key=delivery id, val=disposition plf

    def FrameCount(self):
        count = 0
        for link in self.link_list:
            count += len(link.frame_list)
        count += len(self.session_frame_list)
        return count

    def FindLinkByName(self, attach_name, link_name_unambiguous, parsed_log_line):
        # find conflicted name
        cnl = None
        if attach_name in self.link_name_conflict_map:
            cnl = self.link_name_conflict_map[attach_name]
            if cnl.input_handle == -1 and cnl.output_handle == -1:
                cnl = None
        # find non-conflicted name
        nl = None
        if link_name_unambiguous in self.link_name_to_detail_map:
            nl = self.link_name_to_detail_map[link_name_unambiguous]
            if nl.input_handle == -1 and nl.output_handle == -1:
                nl = None
        # report conflict
        # TODO: There's an issue with this logic generating false positives
        # if nl is None and (not cnl is None):
        #     parsed_log_line.data.amqp_error = True
        #     parsed_log_line.data.web_show_str += " <span style=\"background-color:yellow\">Link name conflict</span>"
        # return unambiguous link
        return nl

    def FindLinkByHandle(self, handle, find_remote):
        """
        Find the current link by handle number
        qualify lookup based on packet direction
        :param link: the performative channel
        :param dst_is_broker: packet direction
        :return: the session or None
        """
        if find_remote:
            return self.input_handle_link_map[handle] if handle in self.input_handle_link_map else None
        else:
            return self.output_handle_link_map[handle] if handle in self.output_handle_link_map else None

    def GetId(self):
        return self.conn_detail.GetId() + "_" + str(self.conn_epoch)

    def GetSeqNo(self):
        self.seq_no += 1
        return self.seq_no

    def DetachOutputHandle(self, handle):
        # take existing link out of session handle map
        if handle in self.output_handle_link_map:
            nl = self.output_handle_link_map[handle]
            del self.output_handle_link_map[handle]
            nl.output_handle = -1

    def DetachInputHandle(self, handle):
        # take existing link out of session remote handle map
        if handle in self.input_handle_link_map:
            nl = self.input_handle_link_map[handle]
            del self.input_handle_link_map[handle]
            nl.input_handle = -1

    def DetachHandle(self, handle, is_remote):
        if is_remote:
            self.DetachInputHandle(handle)
        else:
            self.DetachOutputHandle(handle)

    def GetLinkEventCount(self):
        c = 0
        for link in self.link_list:
            c += link.GetLinkEventCount()
        return c


class LinkDetail():
    """
    Holds facts about a link endpoint
    This structure binds input and output links with same name
    """

    def __init__(self, id, session_detail, session_seq, link_name, start_time):
        self.id = id
        # parent session
        self.session_detail = session_detail

        # some seq number
        self.session_seq = session_seq

        # link name
        self.name = link_name  # plf.data.link_short_name
        self.display_name = link_name  # show short name; hover to see long name

        # Timing
        self.time_start = start_time
        self.time_end = start_time

        # combined counts
        self.counts = Counts()

        self.unsettled_list = []

        # paired handles
        self.output_handle = -1
        self.input_handle = -1

        # link originator
        self.direction = ""
        self.is_receiver = True
        self.first_address = ''

        # set by sender
        self.snd_settle_mode = ''
        self.sender_target_address = "none"
        self.sender_class = ''

        # set by receiver
        self.rcv_settle_mode = ''
        self.receiver_source_address = "none"
        self.receiver_class = ''

        self.frame_list = []

    def GetId(self):
        return self.session_detail.GetId() + "_" + str(self.session_seq)

    def FrameCount(self):
        return len(self.frame_list)


class AllDetails():
    #
    #
    def format_errors(self, n_errors):
        return ("<span style=\"background-color:%s\">errors: %d</span>" % (common.color_of("errors"), n_errors)) if n_errors > 0 else ""

    def format_unsettled(self, n_unsettled):
        return ("<span style=\"background-color:%s\">unsettled: %d</span>" % (common.color_of("unsettled"), n_unsettled)) if n_unsettled > 0 else ""

    def classify_connection(self, id):
        """
        Return probable connection class based on the kinds of links the connection uses.
        TODO: This assumes that the connection has one session and one
        :param id:
        :return:
        """
        return "oops"

    def time_offset(self, ttest, t0):
        """
        Return a string time delta between two datetime objects in seconds formatted
        to six significant decimal places.
        :param ttest:
        :param t0:
        :return:
        """
        if ttest < t0:
            # Never return negative deltas
            return "0.000000"
        delta = ttest - t0
        t = float(delta.seconds) + float(delta.microseconds) / 1000000.0
        return "%0.06f" % t

    def links_in_connection(self, id):
        conn_details = self.conn_details[id]
        n_links = 0
        for sess in conn_details.session_list:
            n_links += len(sess.link_list)
        return n_links

    def settlement_display(self, transfer, disposition):
        """
        Generate the details for a disposition settlement
        :param transfer: plf
        :param disposition: plf
        :return: display string
        """
        state = disposition.data.disposition_state  # accept, reject, release, ...
        if state != "accepted":
            state = "<span style=\"background-color:orange\">%s</span>" % state
        l2disp = "<a href=\"#%s\">%s</a>" % (disposition.fid, state)
        sttld = "settled" if disposition.data.settled == "true" else "unsettled"
        delay = self.time_offset(disposition.datetime, transfer.datetime)
        return "(%s %s %s S)" % (l2disp, sttld, delay)

    def resolve_settlement(self, link, transfer, rcv_disposition, snd_disposition):
        """
        Generate the settlement display string for this transfer.
        :param link: linkDetails - holds settlement modes
        :param transfer: plf of the transfer frame
        :param rcv_disposition: plf of receiver role disposition
        :param snd_disposition: plf of sender   role disposition
        :return: display string
        """
        if transfer.data.settled is not None and transfer.data.settled == "true":
            result = "transfer presettled"
            transfer.data.transfer_presettled = True
            if rcv_disposition is not None:
                sys.stderr.write("WARING: Receiver disposition for presettled message. connid:%s, line:%s\n" %
                                 (rcv_disposition.data.conn_id, rcv_disposition.lineno))
            if snd_disposition is not None:
                sys.stderr.write("WARING: Sender disposition for presettled message. connid:%s, line:%s\n" %
                                 (snd_disposition.data.conn_id, snd_disposition.lineno))
        else:
            if "1" in link.snd_settle_mode:
                # link mode sends only settled transfers
                result = "link presettled"
                transfer.data.transfer_presettled = True
                if rcv_disposition is not None:
                    sys.stderr.write("WARING: Receiver disposition for presettled link. connid:%s, line:%s\n" %
                                     (rcv_disposition.data.conn_id, rcv_disposition.lineno))
                if snd_disposition is not None:
                    sys.stderr.write("WARING: Sender disposition for presettled link. connid:%s, line:%s\n" %
                                     (snd_disposition.data.conn_id, snd_disposition.lineno))
            else:
                # transfer unsettled and link mode requires settlement
                if rcv_disposition is not None:
                    rtext = self.settlement_display(transfer, rcv_disposition)
                    transfer.data.final_disposition = rcv_disposition
                if snd_disposition is not None:
                    stext = self.settlement_display(transfer, snd_disposition)
                    transfer.data.final_disposition = snd_disposition

                if "0" in link.rcv_settle_mode:
                    # one settlement expected
                    if rcv_disposition is not None:
                        result = rtext
                        if snd_disposition is not None:
                            sys.stderr.write("WARING: Sender disposition for single first(0) settlement link. "
                                             "connid:%s, line:%s\n" %
                                             (snd_disposition.data.conn_id, snd_disposition.lineno))
                    else:
                        if transfer.data.transfer_more:
                            result = "(pending)"
                        else:
                            result = "<span style=\"background-color:orange\">%s</span>" % "receive settlement absent"
                else:
                    # two settlements expected
                    if transfer.data.transfer_more:
                        result = "(pending)"
                    elif rcv_disposition is not None:
                        result = "receiver: " + rtext
                        if snd_disposition is not None:
                            result += ", sender: " + stext
                        else:
                            result += "<span style=\"background-color:orange\">%s</span>" % ", sender settlement absent"
                    else:
                        result = "<span style=\"background-color:orange\">%s</span>" % "receiver settlement absent"
                        if snd_disposition is not None:
                            result += ", sender: " + stext
                        else:
                            result += "<span style=\"background-color:orange\">%s</span>" % ", sender settlement absent"
        return result

    def __init__(self, _router, _common):
        self.rtr = _router
        self.comn = _common

        # conn_details - AMQP analysis
        #   key= connection id '1', '2'
        #   val= ConnectionDetails
        # for each connection, for each session, for each link:
        #   what happened
        self.conn_details = {}

        for conn in self.rtr.conn_list:
            id = self.rtr.conn_id(conn)
            self.conn_details[id] = ConnectionDetail(id, self.rtr, conn)
            conn_details = self.conn_details[id]
            conn_frames = self.rtr.conn_to_frame_map[id]
            for plf in conn_frames:
                pname = plf.data.name
                if plf.data.amqp_error:
                    conn_details.counts.errors += 1
                if pname in ['', 'open', 'close']:
                    conn_details.unaccounted_frame_list.append(plf)
                    continue
                # session required
                channel = plf.data.channel  # Assume in/out channels are the same for the time being
                sess_details = conn_details.FindSession(channel)
                if sess_details is None:
                    new_id = len(conn_details.session_list)
                    sess_details = SessionDetail(new_id, conn_details, conn_details.GetSeqNo(), plf.datetime)
                    conn_details.session_list.append(sess_details)
                    conn_details.EndChannel(channel)
                    conn_details.chan_map[channel] = sess_details
                    sess_details.direction = plf.data.direction
                    sess_details.channel = channel
                if plf.data.amqp_error:
                    sess_details.counts.errors += 1

                if pname in ['begin', 'end', 'disposition']:
                    sess_details.session_frame_list.append(plf)  # Accumulate to current session
                    if pname == 'end':
                        # end is closing this session
                        if sess_details.half_closed:
                            conn_details.EndChannel(plf.data.channel)
                        else:
                            sess_details.half_closed = True
                    else:
                        pass  # begin handled above; disposition needs no action

                elif pname in ['attach']:
                    handle = plf.data.handle  # proton local handle
                    link_name = plf.data.link_short_name
                    link_name_unambiguous = link_name + "_" + str(handle)
                    error_was = plf.data.amqp_error
                    nl = sess_details.FindLinkByName(link_name, link_name_unambiguous, plf)
                    # if finding an ambiguous link name generated an error then propagate to session/connection
                    if not error_was and plf.data.amqp_error:
                        conn_details.counts.errors += 1
                        sess_details.counts.errors += 1
                    if nl is None:
                        # Creating a new link from scratch resulting in a half attached link pair
                        new_id = len(sess_details.link_list)
                        nl = LinkDetail(new_id, sess_details, sess_details.GetSeqNo(), link_name, plf.datetime)
                        sess_details.link_list.append(nl)
                        sess_details.link_name_to_detail_map[link_name_unambiguous] = nl
                        sess_details.link_name_conflict_map[link_name] = nl
                        nl.display_name = plf.data.link_short_name_popup
                        nl.direction = plf.data.direction
                        nl.is_receiver = plf.data.role == "receiver"
                        nl.first_address = plf.data.source if nl.is_receiver else plf.data.target
                    if plf.data.amqp_error:
                        nl.counts.errors += 1

                    if plf.data.direction_is_in():
                        # peer is creating link
                        nl.input_handle = handle
                        sess_details.DetachInputHandle(handle)
                        sess_details.input_handle_link_map[handle] = nl
                    else:
                        # local is creating link
                        nl.output_handle = handle
                        sess_details.DetachOutputHandle(handle)
                        sess_details.output_handle_link_map[handle] = nl
                    if plf.data.is_receiver:
                        nl.rcv_settle_mode = plf.data.rcv_settle_mode
                        nl.receiver_source_address = plf.data.source
                        nl.receiver_class = plf.data.link_class
                    else:
                        nl.snd_settle_mode = plf.data.snd_settle_mode
                        nl.sender_target_address = plf.data.target
                        nl.sender_class = plf.data.link_class
                    nl.frame_list.append(plf)

                elif pname in ['detach']:
                    ns = conn_details.FindSession(channel)
                    if ns is None:
                        conn_details.unaccounted_frame_list.append(plf)
                        continue
                    handle = plf.data.handle
                    nl = ns.FindLinkByHandle(handle, plf.data.direction_is_in())
                    ns.DetachHandle(handle, plf.data.direction_is_in())
                    if nl is None:
                        ns.session_frame_list.append(plf)
                    else:
                        if plf.data.amqp_error:
                            nl.counts.errors += 1
                        nl.frame_list.append(plf)

                elif pname in ['transfer', 'flow']:
                    ns = conn_details.FindSession(channel)
                    if ns is None:
                        conn_details.unaccounted_frame_list.append(plf)
                        plf.no_parent_link = True
                        continue
                    handle = plf.data.handle
                    nl = ns.FindLinkByHandle(handle, plf.data.direction_is_in())
                    if nl is None:
                        ns.session_frame_list.append(plf)
                        plf.no_parent_link = True
                    else:
                        if plf.data.amqp_error:
                            nl.counts.errors += 1
                        nl.frame_list.append(plf)
        # identify and index dispositions
        for conn in self.rtr.conn_list:
            id = self.rtr.conn_id(conn)
            conn_detail = self.conn_details[id]
            for sess in conn_detail.session_list:
                # for each disposition add state to disposition_map
                for splf in sess.session_frame_list:
                    if splf.data.name == "disposition":
                        if splf.data.direction == "<-":
                            sdispmap = sess.rx_rcvr_disposition_map if splf.data.is_receiver else sess.rx_sndr_disposition_map
                        else:
                            sdispmap = sess.tx_rcvr_disposition_map if splf.data.is_receiver else sess.tx_sndr_disposition_map
                        for sdid in range(int(splf.data.first), (int(splf.data.last) + 1)):
                            did = str(sdid)
                            if did in sdispmap:
                                old_splf = sdispmap[did]
                                if "state=@received" in old_splf.line:
                                    # Existing disposition is non-terminal.
                                    # Don't complain when it is overwritten by another non-terminal
                                    # or by a terminal disposition.
                                    pass
                                else:
                                    # Current state is terminal disposition. Complain when overwritten.
                                    sys.stderr.write("ERROR: Delivery ID collision in disposition map. connid:%s, \n" %
                                                     (splf.data.conn_id))
                                    sys.stderr.write("  old: %s, %s\n" % (old_splf.fid, old_splf.line))
                                    sys.stderr.write("  new: %s, %s\n" % (splf.fid, splf.line))
                            sdispmap[did] = splf

    def rollup_disposition_counts(self, state, conn, sess, link):
        if state is not None:
            if state.startswith("acce"):
                conn.accepted += 1
                sess.accepted += 1
                link.accepted += 1
            elif state.startswith("reje"):
                conn.rejected += 1
                sess.rejected += 1
                link.rejected += 1
            elif state.startswith("rele"):
                conn.released += 1
                sess.released += 1
                link.released += 1
            elif state.startswith("modi"):
                conn.modified += 1
                sess.modified += 1
                link.modified += 1
            else:
                pass    # Hmmm, some other disposition. TODO: count these

    def compute_settlement(self):
        for conn in self.rtr.conn_list:
            id = self.rtr.conn_id(conn)
            conn_detail = self.rtr.details.conn_details[id]
            for sess in conn_detail.session_list:
                for link in sess.link_list:
                    for plf in link.frame_list:
                        if plf.data.transfer:
                            tdid = plf.data.delivery_id
                            if plf.data.direction == "->":
                                rmap = sess.rx_rcvr_disposition_map
                                tmap = sess.rx_sndr_disposition_map
                            else:
                                rmap = sess.tx_rcvr_disposition_map
                                tmap = sess.tx_sndr_disposition_map
                            plf.data.disposition_display = self.resolve_settlement(link, plf,
                                                                                   rmap.get(tdid),
                                                                                   tmap.get(tdid))
                            if common.transfer_is_possibly_unsettled(plf):
                                if tdid not in link.unsettled_list:
                                    link.unsettled_list.append(tdid)
                                    link.counts.unsettled += 1
                                    sess.counts.unsettled += 1
                                    conn_detail.counts.unsettled += 1

                            else:
                                if not plf.data.transfer_more:
                                    if plf.data.transfer_presettled:
                                        link.counts.presettled += 1
                                        sess.counts.presettled += 1
                                        conn_detail.counts.presettled += 1
                                    else:
                                        self.rollup_disposition_counts(
                                            plf.data.final_disposition.data.disposition_state, conn_detail.counts, sess.counts, link.counts)
                                else:
                                    link.counts.more += 1
                                    sess.counts.more += 1
                                    conn_detail.counts.more += 1
                            if plf.data.transfer_aborted:
                                link.counts.aborted += 1
                                sess.counts.aborted += 1
                                conn_detail.counts.aborted += 1
                        if plf.data.flow_drain:
                            link.counts.drain += 1
                            sess.counts.drain += 1
                            conn_detail.counts.drain += 1

    def index_addresses(self):
        for conn in self.rtr.conn_list:
            id = self.rtr.conn_id(conn)
            conn_detail = self.rtr.details.conn_details[id]
            for sess in conn_detail.session_list:
                for link in sess.link_list:
                    self.comn.shorteners.short_addr_names.translate(link.first_address, False, link)

    def evaluate_credit(self):
        for conn in self.rtr.conn_list:
            id = self.rtr.conn_id(conn)
            conn_detail = self.rtr.details.conn_details[id]
            for sess in conn_detail.session_list:
                for link in sess.link_list:
                    # ignore links without starting attach
                    if link.frame_list[0].data.name != "attach":
                        link.counts.credit_not_evaluated += 1
                        sess.counts.credit_not_evaluated += 1
                        conn_detail.counts.credit_not_evaluated += 1
                        break
                    # process flaggage
                    look_for_sender_delivery_id = True
                    dir_of_xfer = ''
                    dir_of_flow = ''
                    current_delivery = 0  # next transfer expected id
                    delivery_limit = 0  # first unreachable delivery id from flow
                    n_attaches = 0
                    tod_of_second_attach = None
                    multiframe_in_progress = False
                    init_stall = True
                    credit_stall = False
                    tod_of_no_credit = None
                    tod_of_shutdown = None
                    # record info about initial attach
                    is_rcvr = link.frame_list[0].data.is_receiver
                    o_dir = link.frame_list[0].data.direction
                    # derive info about where to look for credit and transfer id
                    #  role dir  transfers flow w/credit case
                    #  ---- ---- --------- ------------- ----
                    #  rcvr  <-   ->        <-            A
                    #  rcvr  ->   <-        ->            B
                    #  sndr  <-   <-        ->            B
                    #  sndr  ->   ->        <-            A
                    #
                    if (((is_rcvr) and (o_dir == text.direction_in())) or
                            ((not is_rcvr) and (o_dir == text.direction_out()))):
                        # case A
                        dir_of_xfer = text.direction_out()
                        dir_of_flow = text.direction_in()
                    else:
                        # case B
                        dir_of_xfer = text.direction_in()
                        dir_of_flow = text.direction_out()

                    for plf in link.frame_list:
                        # initial credit delay starts at reception of second attach
                        if n_attaches < 2:
                            if plf.data.name == "attach":
                                n_attaches += 1
                                if n_attaches == 2:
                                    tod_of_second_attach = plf.datetime
                        if look_for_sender_delivery_id:
                            if plf.data.name == "attach" and not plf.data.is_receiver:
                                current_delivery = int(plf.data.described_type.dict.get("initial-delivery_count", "0"))
                                delivery_limit = current_delivery
                                look_for_sender_delivery_id = False

                        if plf.data.name == "flow":
                            if plf.data.direction == dir_of_flow:
                                # a flow in the normal direction updates the delivery limit
                                dc = plf.data.described_type.dict.get("delivery-count", "0")
                                lc = plf.data.described_type.dict.get("link-credit", "0")
                                delivery_limit = int(dc) + int(lc)  # TODO: wrap at 32-bits
                                if n_attaches < 2:
                                    # a working flow before sender attach - cancel initial stall
                                    init_stall = False
                                if init_stall:
                                    init_stall = False
                                    dur = plf.datetime - tod_of_second_attach
                                    link.counts.initial_no_credit_duration = dur
                                    sess.counts.initial_no_credit_duration += dur
                                    conn_detail.counts.initial_no_credit_duration += dur
                                if credit_stall and delivery_limit > current_delivery:  # TODO: wrap
                                    credit_stall = False
                                    plf.data.web_show_str += " <span style=\"background-color:%s\">credit restored</span>" % common.color_of("no_credit")
                                    dur = plf.datetime - tod_of_no_credit
                                    link.counts.no_credit_duration += dur
                                    sess.counts.no_credit_duration += dur
                                    conn_detail.counts.no_credit_duration += dur
                            else:
                                # flow in the opposite direction updates the senders current delivery
                                # normally used to consume credit in response to a drain from receiver
                                current_delivery = int(plf.data.described_type.dict.get("initial-delivery_count", "0"))

                        elif plf.data.transfer:
                            if plf.data.direction == dir_of_xfer:
                                if not plf.data.transfer_more:
                                    # consider the transfer to have arrived when last transfer seen
                                    current_delivery += 1  # TODO: wrap at 32-bits
                                    if current_delivery == delivery_limit:
                                        link.counts.no_credit += 1
                                        sess.counts.no_credit += 1
                                        conn_detail.counts.no_credit += 1
                                        plf.data.transfer_exhausted_credit = True
                                        credit_stall = True
                                        plf.data.web_show_str += " <span style=\"background-color:%s\">no more credit</span>" % common.color_of("no_credit")
                                        tod_of_no_credit = plf.datetime
                                    else:
                                        pass  # still have credit
                                    multiframe_in_progress = False
                                else:
                                    # transfers with 'more' set don't consume credit
                                    multiframe_in_progress = True
                            else:
                                pass   # transfer in wrong direction??

                        elif plf.data.name == "detach":
                            tod_of_shutdown = plf.datetime
                            break

                    # clean up lingering credit stall
                    if init_stall or credit_stall:
                        if tod_of_shutdown is None:
                            # find first end or close and call that shutdown time
                            for plf in sess.session_frame_list:
                                if plf.data.name == "end":
                                    tod_of_shutdown = plf.datetime
                                    break
                            if tod_of_shutdown is None:
                                for plf in conn_detail.unaccounted_frame_list:
                                    if plf.data.name == "close":
                                        tod_of_shutdown = plf.datetime
                                        break
                                if tod_of_shutdown is None:
                                    # Hmmm, no shutdown. Use last link frame
                                    tod_of_shutdown = link.frame_list[-1].datetime
                        if tod_of_second_attach is None:
                            # Hmmm, no second attach. Use first link frame time
                            tod_of_second_attach = link.frame_list[0].datetime
                        if init_stall:
                            dur = tod_of_shutdown - tod_of_second_attach
                            link.counts.initial_no_credit_duration = dur
                            sess.counts.initial_no_credit_duration += dur
                            conn_detail.counts.initial_no_credit_duration += dur
                        if credit_stall:  # TODO: wrap
                            dur = tod_of_shutdown - tod_of_no_credit
                            link.counts.no_credit_duration += dur
                            sess.counts.no_credit_duration += dur
                            conn_detail.counts.no_credit_duration += dur

                    # record multiframe transfer that didn't complete
                    if multiframe_in_progress:
                        link.counts.incomplete += 1
                        sess.counts.incomplete += 1
                        conn_detail.counts.incomplete += 1

    def show_html(self):
        for conn in self.rtr.conn_list:
            id = self.rtr.conn_id(conn)
            conn_detail = self.rtr.details.conn_details[id]
            conn_frames = self.rtr.conn_to_frame_map[id]
            print("<a name=\"cd_%s\"></a>" % id)
            # This lozenge shows/hides the connection's data
            print("<a href=\"javascript:toggle_node('%s_data')\">%s%s</a>" %
                  (id, text.lozenge(), text.nbsp()))
            dir = self.rtr.conn_dir[id] if id in self.rtr.conn_dir else ""
            peer = self.rtr.conn_peer_display.get(id, "")  # peer container id
            peerconnid = self.comn.conn_peers_connid.get(id, "")
            # show the connection title
            print("%s %s %s %s (nFrames=%d) %s<br>" %
                  (id, dir, peerconnid, peer, len(conn_frames), conn_detail.counts.show_html()))
            # data div
            print("<div id=\"%s_data\" style=\"display:none; margin-bottom: 2px; margin-left: 10px\">" % id)

            # unaccounted frames
            print("<a href=\"javascript:toggle_node('%s_data_unacc')\">%s%s</a>" %
                  (id, text.lozenge(), text.nbsp()))
            # show the connection-level frames
            errs = sum(1 for plf in conn_detail.unaccounted_frame_list if plf.data.amqp_error)
            print("Connection-based entries %s<br>" % self.format_errors(errs))
            print("<div id=\"%s_data_unacc\" style=\"display:none; margin-bottom: 2px; margin-left: 10px\">" % id)
            for plf in conn_detail.unaccounted_frame_list:
                print(plf.adverbl_link_to(), plf.datetime, plf.data.direction, peer, plf.data.web_show_str, "<br>")
            print("</div>")  # end unaccounted frames

            # loop to print session details
            for sess in conn_detail.session_list:
                # show the session 'toggle goto' and title
                print("<a href=\"javascript:toggle_node('%s_sess_%s')\">%s%s</a>" %
                      (id, sess.conn_epoch, text.lozenge(), text.nbsp()))
                print("Session %s: channel: %s, peer channel: %s; Time: start %s, Counts: frames: %d %s<br>" %
                      (sess.conn_epoch, sess.channel, sess.peer_chan, sess.time_start,
                       sess.FrameCount(), sess.counts.show_html()))
                print("<div id=\"%s_sess_%s\" style=\"display:none; margin-bottom: 2px; margin-left: 10px\">" %
                      (id, sess.conn_epoch))
                # show the session-level frames
                errs = sum(1 for plf in sess.session_frame_list if plf.data.amqp_error)
                print("<a href=\"javascript:toggle_node('%s_sess_%s_unacc')\">%s%s</a>" %
                      (id, sess.conn_epoch, text.lozenge(), text.nbsp()))
                print("Session-based entries %s<br>" % self.format_errors(errs))
                print("<div id=\"%s_sess_%s_unacc\" style=\"display:none; margin-bottom: 2px; margin-left: 10px\">" %
                      (id, sess.conn_epoch))
                for plf in sess.session_frame_list:
                    print(plf.adverbl_link_to(), plf.datetime, plf.data.direction, peer, plf.data.web_show_str, "<br>")
                print("</div>")  # end <id>_sess_<conn_epoch>_unacc
                # loops to print session link details
                # first loop prints link table
                print("<table")
                print("<tr><th>Link</th> <th>Dir</th> <th>Role</th>  <th>Address</th>  <th>Class</th>  "
                      "<th>snd-settle-mode</th>  <th>rcv-settle-mode</th>  <th>Start time</th>  <th>Frames</th> "
                      "<th>Counts</th> </tr>")
                for link in sess.link_list:
                    # show the link toggle and title
                    showthis = ("<a href=\"javascript:toggle_node('%s_sess_%s_link_%s')\">%s</a>" %
                                (id, sess.conn_epoch, link.session_seq, text.lozenge()))
                    visitthis = ("<a href=\"#%s_sess_%s_link_%s_data\">%s</a>" %
                                 (id, sess.conn_epoch, link.session_seq, link.display_name))
                    role = "receiver" if link.is_receiver else "sender"
                    print("<tr><td>%s %s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
                          "<td>%s</td><td>%d</td><td>%s</td> </tr>" %
                          (showthis, visitthis, link.direction, role, link.first_address,
                           (link.sender_class + '-' + link.receiver_class), link.snd_settle_mode,
                           link.rcv_settle_mode, link.time_start, link.FrameCount(),
                           link.counts.show_html()))
                print("</table>")
                # second loop prints the link's frames
                for link in sess.link_list:
                    print(
                        "<div id=\"%s_sess_%s_link_%s\" style=\"display:none; margin-top: 2px; margin-bottom: 2px; margin-left: 10px\">" %
                        (id, sess.conn_epoch, link.session_seq))
                    print("<a name=\"%s_sess_%s_link_%s_data\"></a>" %
                          (id, sess.conn_epoch, link.session_seq))
                    print("<h4>Connection %s Session %s Link %s</h4>" %
                          (id, sess.conn_epoch, link.display_name))
                    for plf in link.frame_list:
                        print(plf.adverbl_link_to(), plf.datetime, plf.data.direction, peer, plf.data.web_show_str,
                              plf.data.disposition_display, "<br>")
                    print("</div>")  # end link <id>_sess_<conn_epoch>_link_<sess_seq>

                print("</div>")  # end session <id>_sess_<conn_epoch>

            print("</div>")  # end current connection data


if __name__ == "__main__":

    try:
        pass
    except:
        traceback.print_exc(file=sys.stdout)
        pass
