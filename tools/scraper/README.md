#  Scraper - Analyze qpid-dispatch log files

Scraper provides two analysis modes:

 * Normal mode: combine logs and show details.
 * Split mode: split a single log into per-connection data and show details.
 
Details are written to stdout in html format. 

## Apache License, Version 2.0

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.


## Normal Mode

Scraper is a data scraping program. It reads qpid-dispatch router log files,
categorizes and sorts the data, and produces an HTML summary. In normal mode
Scraper can operate on a single log file or on many log files.

Normal mode does not write any files to your file system. The result is printed on stdout.

From each log file Scraper extracts:

 * Router version
 * Router container name
 * Router restart times. A single log file may contain data from several router
   reboot instances.
 * Router link state calculation reports
 * Interrouter and client connections
 * AMQP facts
   * Connection peers
   * Link pair establishment
   * Transfer traffic
   * Message disposition
   * Flow and credit propagation
   
 Scraper sorts these facts with microsecond precision using the log timestamps.
 
 Then Scraper merges the data from any number (as long as that number is less than 27!)
 of independent log files into a single view.
 
 Next Scraper performs some higher-level analysis.
 
 * Routers are identified by letter rather than by the container name: 'A', 'B', and
   so on. Log data in a file is grouped into instances and is identified by a number
   for that router instance: 'A0', 'A1', and so on.
 * Per router each AMQP data log entry is sorted into per-connection data lists.
 * Connection data lists are searched to discover router-to-router and router-to-client
   connection pairs.
 * Per connection data are subdivided into per-session and per-link lists, sorting
   the AMQP data into per-link-only views. This information is shown in the _Connection 
   Details_ section. Clicking on the lozenge will expand a connection into a view that 
   shows all the sessions. Clicking on a session lozenge will expand into a view that
   shows all the links.
   
   Each link shows a lozenge that will toggle the link data visibility and a hyperlink
   that will go to the link data. When the table of links is small and visible on one
   screen then it's easy to see what's going on. But when the table of links is large,
   say 2000 links, then the data for each link is 2000 lines later in the web page.
   When you click the lozenge you don't see the data. For larger data sets the usage
   rule is to first click the lozenge to expose the data, and then click the hyperlink
   go make the data visible.
 
 * For each link the first AMQP Attach is examined to discover the link's source or
   target address. All the addresses named in all the links are aggregated into a table.
   
   To view the link traffic first click the lozenge in the Link column to make the data
   visible and then click the hyperlinked link name to see the data.
 
 * Bulk AMQP data may be shown or hidden on arbitrary per-connection selections.
   Note: This feature is slow when a few tens of thousands of lines are being shown or
   hidden.
 * Noteworthy AMQP frames are identified. By hand these are hard to find.
   * AMQP errors
   * Presettled transfers
   * Transfers with 'more' bit set
   * Resumed transfers
   * Aborted transfers
   * Flow with 'drain' set
   * Probable unsettled transfers.
   * Possible unsettled transfers. When log files are truncated and not all log lines
     for this connection, session, and link are present then settlement calculations
     are compromised. Transfers may appear to be unsettled but the settlement could
     not be correlated properly with the transfer.
 * Transfer messages are sorted by signature. Then a table is made showing where
   each message leaves or arrives over a connection. The signature is limited to the
   body of the message as shown in the Qpid Proton log text.
 * Settlement state for each unsettled transfer is identified, displayed, and
   shown with delta and elapsed time values. See example in the Advanced section.
 * Link name propagation for each named link is shown in a table.
 * Router, peer, and link names can get really long. Nicknames for each are used
   with popups showing the full name.
 * Transfer data is identified with nicknames but without the popups. The popups
   were so big that Firefox refused to show them; so forget it and they weren't useful anyway.
 * Router link state cost calculations are merged with router restart records to
   create a comprehensive link state cost view. Routers may publish cost reports that
   do not include all other routers. In this case the other routers are identified
   visually to indicate that they are unreachable.

## Split Mode

Split mode was developed to help analyze log files that are generally too big to be
processed by Scraper normal mode. Split mode works equally well on both large and small
log files.

Split mode rewrites all of the log file's AMQP log data into many, possibly tens of
thousands, smaller files. Data is confined to a subdirectory named after the original
log file. Scraper will not overwrite existing output directories.

| Log file   | Scraper output directory|
|------------|-------------------------|
| EB2.log    | EB2.log.splits          |

Split mode directs Scraper to accept a single log file and
break it into per-connection data files. The connections are counted and the volume of
data handled by each connection is noted. Then each connection's data is written
to a subfolder whose name indicates the amount of data the connection handled.

| Log lines   | Directory|
|-------------|----------|
| 250         | 10e3     |
| 45,000      | 10e5     |
| 1,234,567   | 10e7     |


Split mode also analyzes:

* Each router reboot starts a new router _instance_. Split mode observes the reboots
and divides the connection data. Connection names inclued the router instance ID.

    A\<instance\>_\<connectionId\>

* Connection peers are identified. Scraper currently recognizes these peers:

  * Inter-router
  * Apache activemq-artemis broker
  * Apache qpidd broker
  * Apache Qpid-JMS client

* Scraper searches all log lines for AMQP errors and produces a table identifying them.
* Connections are listed in order based on
  * Total transfer count
  * Total log line count
  * Connections with zero transfers
* Per-connection files are linked from the connection lists for one-click viewing
* AMQP Addresses from every every AMQP Attach are indexed. A table for each address
  shows when the address was referenced and some connection details.

## AMQP performative legend

Scraper decorates performative display lines with important AMQP values. 

| Performative | Decorations                 |
|--------------|-----------------------------|
| open         | [0]                         |
| close        | [0]                         |
| begin        | [channel, remoteChannel]    |
| end          | [channel]                   |
| attach       | [channel, handle] role linkName (source: src, target: tgt) |
| detach       | [channel, handle]           |
| flow         | [channel, handle] (deliveryCount, linkCredit) |
| transfer     | [channel, handle] (deliveryId) .. [flags] length (settement state)      |
| disposition  | [channel] (role firstId-lastId settleFlags settleState) |
 

## Sequence generator

The sequence generator (switches --sequence or -sq) generates some extra output that is
useful for making sequence diagrams from the raw data. _This is an experimental feature._

When a sequence switch is set then the AMQP performatives are simplified and dumped
into the html data stream near the end.

A user then identifies what parts of the log data he needs and then edits the lines
in question from the html data stream file. Careful attention to the start and
end timestamps will help a user find the data more easily. Save the selected data into an
intermediate file.

Finally the intermediate file is processed by new file _tools/scraper/seq-diag-gen.py_
to produce the sequence diagram source file.

Items to do to make this a reliable feature:

* In most cases the the non-instantaneous messages do not exactly line up on a precisely
between the sender's and receiver's time lines.
* The vertical time scale is wildly exaggerated. There is no scale: equal intervals in
vertical space could be microseconds or days.
* Messages from non-router actors are shown as horizontal lines. This tool has no access
to those actors' trace logs or timestamps.
* You may want to manually specify actors and participants to get the vertical time lines
in an acceptable order.

## Quick Start

* Enable router logging

The routers need to generate proper logging for Scraper.
Expose the information in router logs that Scraper requires by 
enabling specific log levels for some of the router logging modules.

| Log module and level | Information exposed in logs |
|----------------|---------------------------|
| ROUTER info    | Router version            |
| SERVER info    | Router restart discovery  |
| SERVER trace   | AMQP control and data - versions 1.10 and earlier |
| PROTOCOL trace | AMQP control and data - versions 1.11 and later   |
| ROUTER_LS info | Router link state reports |

* Enable version-specific router logging

| Version          | SERVER trace                | PROTOCOL trace         |
|------------------|-----------------------------|------------------------|
| 1.10 and earlier | Required                    | Error - do not specify |
| 1.11 and later   | Do not specify - clutters log file with info that Scraper does not use | Required |

* Run your tests to populate log files used as Scraper input.

* Run Scraper to generate web content

    tools/scraper/scraper.py -f somefile.log > somefile.html

    tools/scraper/scraper.py -f *.log > somefile.html

* Profit

    firefox somefile.html

## Scraper command line

    usage: scraper.py [-h] [--skip-all-data] [--skip-detail] [--skip-msg-progress]
                      [--split] [--time-start TIME_START] [--time-end TIME_END]
                      [--files FILES [FILES ...]]
    
    optional arguments:
      -h, --help            show this help message and exit
      --skip-all-data, -sa  Max load shedding: do not store/index transfer,
                            disposition, flow, or EMPTY_FRAME data
      --skip-detail, -sd    Load shedding: do not produce Connection Details
                            tables
      --skip-msg-progress, -sm
                            Load shedding: do not produce Message Progress tables
      --split, -sp          A single file is split into per-connection data.
      --time-start TIME_START, -ts TIME_START
                            Ignore log records earlier than this. Format:
                            "2018-08-13 13:15:00.123456"
      --time-end TIME_END, -te TIME_END
                            Ignore log records later than this. Format:
                            "2018-08-13 13:15:15.123456"
      --files FILES [FILES ...], -f FILES [FILES ...]

* Split mode works with a single file and ignores all other switches
* Normal mode (no --split switch) accepts other switches and multiple files

### Switch --skip-all-data
    tools/scraper/scraper.py --skip-all-data -f FILE [FILE ...]
    
With _--skip-all-data_ AMQP transfer, disposition, and flow frames in the log files are
discarded. The resulting web page still includes lots of useful information with
connection info, link name propagation, and link state analysis.

### Switch --skip-detail
    tools/scraper/scraper.py --skip-detail -f FILE [FILE ...]
    
With _--skip-detail_ the display of per-connection, per-session, per-link tables is skipped.

### Switch --skip-msg-progress
    tools/scraper/scraper.py --skip-msg-progress -f FILE [FILE ...]
    
With _--skip-msg-progress_ the display of transfer analysis tables is skipped.
###  Advanced

* Merging multiple qpid-dispatch log files

Scraper accepts multiple log files names in the command line and
merges the log data according to the router log timestamps.

    tools/scraper/scraper.py --files A.log B.log C.log > abc.html

Note that the qpid-dispatch host system clocks for merged log files
must be synchronized to within a few microseconds in order for the
result to be useful. This is easiest to achieve when the routers are
run on the same CPU core on a single system. Running Fedora 27 and 28
on two hosts in a router network where the routers run _ntp_ to the same
time provider produces perfectly acceptable results.

Scraper does a decent job merging log files created within a
qpid-dispatch self test.

* Wow, that's a lot of data

Indeed it is and good luck figuring it out. Sometimes, though, it's too much.
The AMQP transfer data analysis is the worst offender in terms of CPU time, 
run-time memory usage, and monstrous html output files.
Scraper provides command line switches to
turn off sections of the data analysis.

* How to read the transfer analysis tables. Here's an instance:


|Src    |Time           |Rtr|ConnId|Dir|ConnId|Peer  |T delta  |T elapsed |Settlement                   |S elapsed
|-------|---------------|---|------|---|------|------|---------|----------|-----------------------------|---------
|A0_2035|09:50:52.027975|A0 |A0_11 |<- |      |peer_7|0.000000 |0.000000  |(accepted settled 0.005142 S)|0.005142
|A0_2071|09:50:52.028556|A0 |A0_6  |-> |D0_4  |D     |0.000581 |0.000581  |(accepted settled 0.004253 S)|0.004834
|D0_1587|09:50:52.028696|D0 |D0_4  |<- |A0_6  |A     |0.000140 |0.000721  |(accepted settled 0.003988 S)|0.004709
|D0_1612|09:50:52.029260|D0 |D0_1  |-> |C0_6  |C     |0.000564 |0.001285  |(accepted settled 0.003044 S)|0.004329
|C0_1610|09:50:52.029350|C0 |C0_6  |<- |D0_1  |D     |0.000090 |0.001375  |(accepted settled 0.002846 S)|0.004221
|C0_1625|09:50:52.029672|C0 |C0_1  |-> |B0_5  |B     |0.000322 |0.001697  |(accepted settled 0.002189 S)|0.003886
|B0_1438|09:50:52.029760|B0 |B0_5  |<- |C0_1  |C     |0.000088 |0.001785  |(accepted settled 0.002002 S)|0.003787
|B0_1451|09:50:52.030117|B0 |B0_7  |-> |      |peer_7|0.000357 |0.002142  |(accepted settled 0.001318 S)|0.003460

Each row in this table represents the facts about when a single transfer and its corresponding settlement was seen entering or exiting a router.

| Field        | Contents |
|--------------|----------|
|Src | Router instance and file line number where the transfer was seen|
| Time | timestamp
| Rtr | Router letter id and instance
| ConnId | Router connection id
| Dir | transfer direction. _<-_ indicates into the router, _->_ indicates out of the router
| ConnId | peer's connection id. Blank if the peer is a normal client and not a router.
| Peer | Peer's name. _peer7_ whold show the peer's container name in a popup.
| T delta | Time since previous row
| T elapsed | Time since the message first entered the system
| Settlement | Settlement state and time delta since message time in column 2 for this row. The settlement disposition log line is hyperlinked from the word _accepted_.
| S elapsed | Settlement elapsed time. This is the difference between the accepted disposition log record and the time when the message first entered the system.

Row-by-row it is easiest to read the each line from left to right
* A0 connecton 11 received the transfer from peer_7.
* A0 connection 6 sent the message to D0 connection 4.
* D0 connection 4 received the message from A0 connection 6.

and so on. This message came from a sender on peer_7, went through routers A, D, C, and B, and finally was
returned to a listener on peer_7. The receiver received the message 0.002142 S after the sender sent it. The
sender received the accepted disposition 0.005142 S after the sender sent the message.

The transmit times are in order from top to bottom and the settlement times are in order from bottom to top.
