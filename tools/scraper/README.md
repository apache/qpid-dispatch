#  Scraper - Render qpid-dispatch log files

Scraper is a spinoff of https://github.com/ChugR/Adverb that uses qpid-dispatch log
files as the data source instead of pcap trace files. Scraper is a Python processing
engine that does not require Wireshark or any network trace capture utilities.

## Apache License, Version 2.0

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.


## Concepts

Scraper is a data scraping program. It reads qpid-dispatch router log files,
categorizes and sorts the data, and produces an HTML summary.

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
   the AMQP data into per-link-only views.
 * Bulk AMQP data may be shown or hidden on arbitrary per-connection selections.
 * Noteworthy AMQP frames are identified. By hand these are hard to find.
   * AMQP errors
   * Presettled transfers
   * Transfers with 'more' bit set
   * Resumed transfers
   * Aborted transfers
   * Flow with 'drain' set
 * Transfer messages are sorted by signature. Then a table is made showing where
   each message leaves or arrives over a connection.
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

### The basics

* Enable router logging

The routers need to generate proper logging for Scraper.
The information classes are exposed by enabling log levels.

| Log level      | Information               |
|----------------|---------------------------|
| ROUTER info    | Router version            |
| SERVER info    | Router restart discovery  |
| SERVER trace   | AMQP control and data     |
| ROUTER_LS info | Router link state reports |


* Run your tests to populate log files used as Scraper input.

* Run Scraper to generate web content

    bin/scraper/main.py somefile.log > somefile.html

    bin/scraper/mina.py *.log > somefile.html

* Profit

    firefox somefile.html

###  Advanced

* Merging multiple qpid-dispatch log files

Scraper accepts multiple log files names in the command line and
merges the log data according to the router log timestamps.

    bin/scraper/main.py A.log B.log C.log > abc.html

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
Scraper provides one command line switch to
turn off the data analysis:

    bin/scraper/main.py --no-data FILE [FILE ...]
    
In no-data mode AMQP transfer, disposition, and flow frames in the log files are
discarded. The resulting web page still includes lots of useful information with
connection info, link name propagation, and link state analysis.

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

This table will morph a little if one of the router is missing from the analysis. If log file D.log was not
presented to Scraper then the table would not make as much sense as when all logs are included.
