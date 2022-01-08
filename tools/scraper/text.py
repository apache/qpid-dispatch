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

"""Common text strings"""


def direction_in():
    """Log line text indicating received by router"""
    return "<-"


def direction_out():
    """Log line text indicating transmitted by router"""
    return "->"


def lozenge():
    """:return: HTML document lozenge character"""
    return "&#9674;"


def nbsp():
    """:return: HTML Non-breaking space"""
    return "&#160;"


"""Large text strings used by main that change infrequently"""


# html head, start body
def web_page_head():
    return """<!DOCTYPE html>
<html>
<head>
<title>Adverbl Analysis - qpid-dispatch router logs</title>

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

function go_back()
{
  window.history.back();
}
"""


def web_page_toc():
    return """
<h3>Contents</h3>
<table>
<tr> <th>Section</th>                                                 <th>Description</th> </tr>
<tr><td><a href=\"#c_logfiles\"       >Log files</a></td>             <td>Router and log file info</td></tr>
<tr><td><a href=\"#c_rtrinstances\"   >Router Instances</a></td>      <td>Router reboot chronology</td></tr>
<tr><td><a href=\"#c_connections\"    >Connections</a></td>           <td>Connection overview; per connection log data view control</td></tr>
<tr><td><a href=\"#c_addresses\"      >Addresses</a></td>             <td>AMQP address usage</td></tr>
<tr><td><a href=\"#c_connectchrono\"  >Connection Chronology</a></td> <td>Router restart and connection chronology</td></tr>
<tr><td><a href=\"#c_conndetails\"    >Connection Details</a></td>    <td>Connection details; frames sorted by link</td></tr>
<tr><td><a href=\"#c_noteworthy\"     >Noteworthy log lines</a></td>  <td>AMQP errors and interesting flags</td></tr>
<tr><td><a href=\"#c_logdata\"        >Log data</a></td>              <td>Main AMQP traffic table</td></tr>
<tr><td><a href=\"#c_messageprogress\">Message progress</a></td>      <td>Tracking messages through the system</td></tr>
<tr><td><a href=\"#c_linkprogress\"   >Link name propagation</a></td> <td>Tracking link names</td></tr>
<tr><td><a href=\"#c_rtrdump\"        >Router name index</a></td>     <td>Short vs. long router container names</td></tr>
<tr><td><a href=\"#c_peerdump\"       >Peer name index</a></td>       <td>Short vs. long peer names</td></tr>
<tr><td><a href=\"#c_linkdump\"       >Link name index</a></td>       <td>Short vs. long link names</td></tr>
<tr><td><a href=\"#c_msgdump\"        >Transfer name index</a></td>   <td>Short names representing transfer data</td></tr>
<tr><td><a href=\"#c_ls\"             >Router link state</a></td>     <td>Link state analysis</td></tr>
<tr><td><a href=\"#c_sequence\"       >Sequence diagram data</a></td> <td>Input data for seq-diag-gen.py utility</td></tr>
</table>
<hr>
"""


if __name__ == "__main__":
    pass
