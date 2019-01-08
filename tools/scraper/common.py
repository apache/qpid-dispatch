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

# Common data storage and utilities

import sys

import nicknamer

IS_PY2 = sys.version_info[0] == 2

if IS_PY2:
    def dict_iteritems(d):
        return d.iteritems()
    def dict_iterkeys(d):
        return d.iterkeys()
else:
    def dict_iteritems(d):
        return iter(d.items())
    def dict_iterkeys(d):
        return iter(d.keys())

class Common():

    # analysis_level_ludicrous
    # Adverbl tries too hard to cross reference data
    # Use these switchs to turn some of the biggest offenders off
    per_link_detail = True
    message_progress_tables = False

    # returned from argparse.parse_args()
    args = None

    # first letter of the connection names
    log_char_base = 'A'

    # number of logs processed
    n_logs = 0

    # array of file name strings from command line
    # len=n_logs
    log_fns = []

    # discovered router container names
    # len=n_logs
    router_ids = [] # raw long names

    # router display names shortened with popups
    router_display_names = []

    # router modes in plain text
    router_modes = []

    # list of router-instance lists
    # [[A0, A1], [B0], [C0, C1, C2]]
    routers = []

    # ordered list of connection names across all routers
    all_conn_names = []

    # conn_details_map -
    # key=conn_id, val=ConnectionDetail for that connection
    conn_details_map = {}

    # mapping of connected routers by connection id
    # A0_1 is connected to B3_2
    # key = full conn_id 'A0_5'
    # val = full conn_id 'B0_8'
    # note names[key]=val and names[val]=key mutual reference
    conn_peers_connid = {}

    # short display name for peer indexed by connection id
    # A0_1 maps to B's container_name nickname
    conn_peers_display = {}

    # conn_to_frame_map - global list for easier iteration in main
    # key = conn_id full A0_3
    # val = list of plf lines
    conn_to_frame_map = {}

    shorteners = nicknamer.Shorteners()

    # when --no-data is in effect, how many log lines were skipped?
    data_skipped = 0

    def router_id_index(self, id):
        """
        Given a router full container name, return the index in router_ids table
        Throw value error if not found
        :param id:
        :return:
        """
        return self.router_ids.index(id)


def log_letter_of(idx):
    '''
    Return the letter A, B, C, ... from the index 0..n
    :param idx:
    :return: A..Z
    '''
    if idx >= 26:
        sys.exit('ERROR: too many log files')
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[idx]

def index_of_log_letter(letter):
    '''
    Return the index 0..25 of the firster letter of the 'letter' string
    Raise error if out of range
    :param letter:
    :return:
    '''
    val = "ABCDEFGHIJKLMNOPQRSTUVWXYZ".find(letter[0].upper())
    if val < 0 or val > 25:
        raise ValueError("index_of_log_letter Invalid log letter: %s", letter)
    return val

class RestartRec():
    def __init__(self, _id, _router, _event, _datetime):
        self.id = _id
        self.router = _router
        self.event = _event
        self.datetime = _datetime

def transfer_is_possibly_unsettled(plf):
    return (plf.data.transfer and not plf.data.transfer_more and
            not (plf.data.transfer_settled or plf.data.final_disposition is not None))

global_colors = {
    "errors": "yellow",
    "unsettled": "tomato",
    "presettled": "aqua",
    "accepted": "aquamarine",
    "rejected": "orange",     # hard coded in resolve_settlement
    "released": "orange",
    "modified": "orange",
    "aborted": "crimson",
    "more": "chartreuse",
    "drain": "gold",
    "no_credit": "beige"
}

def color_of(obj_type):
    return global_colors.get(obj_type, "pink")