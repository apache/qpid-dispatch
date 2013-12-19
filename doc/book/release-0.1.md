;;
;; Licensed to the Apache Software Foundation (ASF) under one
;; or more contributor license agreements.  See the NOTICE file
;; distributed with this work for additional information
;; regarding copyright ownership.  The ASF licenses this file
;; to you under the Apache License, Version 2.0 (the
;; "License"); you may not use this file except in compliance
;; with the License.  You may obtain a copy of the License at
;; 
;;   http://www.apache.org/licenses/LICENSE-2.0
;; 
;; Unless required by applicable law or agreed to in writing,
;; software distributed under the License is distributed on an
;; "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
;; KIND, either express or implied.  See the License for the
;; specific language governing permissions and limitations
;; under the License.
;;

# Qpid Dispatch Release 0.1

## System Requirements and Dependencies

 - Qpid Dispatch will only build and run on Posix-based operating systems (Linux, et. al.)
 - Qpid Proton version 0.6 must be installed (including the Python bindings) to build Qpid Dispatch

## Building, Testing, and Installing

Download and extract the source tar file:

    $ wget http://people.apache.org/~tross/qpid-dispatch-0.1rc3/qpid-dispatch-0.1.tar.gz
    $ tar -xzf qpid-dispatch-0.1.tar.gz

Source the build configuration:

    $ cd qpid-dispatch-0.1
    $ source config.sh

Build and test the package.  This will create two directories: 'build' and 'install'.  Dispatch will be built in the 'build' directory
and installed in the 'install' directory.  The regression and system test suites will then be run against the installed bits.

    $ bin/test.sh

If you wish to change the build configuration, go into the build directory, use cmake to configure your build then rebuild and/or reinstall from there:

    $ cd build
    $ cmake ..
    $ make
    $ make install

## Configuration

The default configuration file is installed in
_install-prefix_/etc/qpid/qdrouterd.conf.  This configuration file will cause the router
to run in standalone mode, listening on the standard AMQP port (5672).  Dispatch Router
looks for the configuration file in the installed location by default.  If you wish
to use a different path, the "-c" command line option will instruct Dispatch Router as to
which configuration to load.

To run the router, invoke the executable:

    $ qdrouterd

## Client Compatibility

Dispatch Router should, in theory, work with any client that is compatible with AMQP 1.0.
The following clients have been tested:

  || *Client* || *Notes* ||
  || qpid::messaging || The Qpid messaging clients work with Dispatch Router as long as they are configured to use the 1.0 version of the protocol.  To enable AMQP 1.0 in the C++ client, use the {protocol:amqp1.0} connection option. ||
  || Proton Messenger || Messenger works with Dispatch Router. ||

## Tools

Installed with the Dispatch Router kit is a command line tool called *qdstat*.  This tool
can be used to view manageable data inside Dispatch Router.  The following options are
useful for seeing that the router is doing:

  || *Option* || *Description* ||
  || -l || Print a list of AMQP links attached to the router.  Links are unidirectional. Outgoing links are usually associated with a subscription address.  The tool distinguishes between _endpoint_ links and _router_ links.  Endpoint links are attached to clients using the router.  Router links are attached to other routers in a network of routers. ||
  || -a || Print a list of addresses known to the router. ||
  || -n || Print a list of known routers in the network. ||
  || -c || Print a list of connections to the router. ||

## Features and Examples

### Standalone and Interior Modes

The router can operate stand-alone or as a node in a network of routers.  The mode is
configured in the _router_ section of the configuration file.  In stand-alone mode, the
router does not attempt to collaborate with any other routers and only routes messages
among directly connected endpoints.

If your router is running in stand-alone mode, _qdstat -a_ will look like the following:

    $ qdstat -a
    Router Addresses
      class  address      in-proc  local  remote  in  out  thru  to-proc  from-proc
      ===============================================================================
      local  $management  Y        0      0       1   0    0     1        0
      local  temp.AY81ga           1      0       0   0    0     0        0

Note that there are two known addresses. _$management_ is the address of the router's embedded management agent.  _temp.AY81ga_ is the temporary reply-to address of the _qdstat_ client making requests to the agent.

If you change the mode to interior and restart the processs, the same command will yield two additional addresses which are used for inter-router communication:

    $ qdstat -a
    Router Addresses
      class  address      in-proc  local  remote  in  out  thru  to-proc  from-proc
      ===============================================================================
      local  $management  Y        0      0       1   0    0     1        0
      local  qdhello      Y        0      0       0   0    0     0        3
      local  qdrouter     Y        0      0       0   0    0     0        1
      local  temp.khOpGb           1      0       0   0    0     0        0


### Mobile Subscribers

The term "mobile subscriber" simply refers to the fact that a client may connect to the
router and subscribe to an address to receive messages sent to that address.  No matter
where in the network the subscriber attaches, the messages will be routed to the
appropriate destination.

To illustrate a subscription on a stand-alone router, you can use the examples that are
provided with Qpid Proton.  Using the _recv.py_ example receiver:

    $ recv.py amqp://0.0.0.0/my-address

This command creates a receiving link subscribed to the specified address.  To verify the
subscription:

    $ qdstat -a
    Router Addresses
      class   address      in-proc  local  remote  in  out  thru  to-proc  from-proc
      ================================================================================
      local   $management  Y        0      0       1   0    0     1        0
      mobile  my-address            1      0       0   0    0     0        0
      local   temp.fDt8_a           1      0       0   0    0     0        0

You can then, in a separate command window, run a sender to produce messages to that
address:

    $ send.py -a amqp://0.0.0.0/my-address

### Dynamic Reply-To

Dynamic reply-to can be used to obtain a reply-to address that routes back to a client's
receiving link regardless of how many hops it has to take to get there.  To illustrate
this feature, see below a simple program (written in C++ against the qpid::messaging API)
that queries the management agent of the attached router for a list of other known
routers' management addresses.

    #include <qpid/messaging/Address.h>
    #include <qpid/messaging/Connection.h>
    #include <qpid/messaging/Message.h>
    #include <qpid/messaging/Receiver.h>
    #include <qpid/messaging/Sender.h>
    #include <qpid/messaging/Session.h>

    using namespace qpid::messaging;
    using namespace qpid::types;

    using std::stringstream;
    using std::string;

    int main() {
        const char* url = "amqp:tcp:127.0.0.1:5672";
        std::string connectionOptions = "{protocol:amqp1.0}";

        Connection connection(url, connectionOptions);
        connection.open();
        Session session = connection.createSession();
        Sender sender = session.createSender("mgmt");

        // create reply receiver and get the reply-to address
        Receiver receiver = session.createReceiver("#");
        Address responseAddress = receiver.getAddress();

    	Message request;
        request.setReplyTo(responseAddress);
        request.setProperty("x-amqp-to", "amqp:/_local/$management");
        request.setProperty("operation", "DISCOVER-MGMT-NODES");
        request.setProperty("type", "org.amqp.management");
        request.setProperty("name, "self");

        sender.send(request);
        Message response = receiver.fetch();
        Variant content(response.getContentObject());
        std::cout << "Response: " << content << std::endl << std::endl;

        connection.close();
    }

The equivalent program written in Python against the Proton Messenger API:

    from proton import Messenger, Message

    def main():
        host = "0.0.0.0:5672"

        messenger = Messenger()
        messenger.start()
        messenger.route("amqp:/*", "amqp://%s/$1" % host)
        reply_subscription = messenger.subscribe("amqp:/#")
        reply_address = reply_subscription.address

        request  = Message()
        response = Message()

        request.address = "amqp:/_local/$management"
        request.reply_to = reply_address
        request.properties = {u'operation' : u'DISCOVER-MGMT-NODES',
                              u'type'      : u'org.amqp.management',
                              u'name'      : u'self'}

        messenger.put(request)
        messenger.send()
        messenger.recv()
        messenger.get(response)

        print "Response: %r" % response.body

        messenger.stop()

    main()


## Known Issues and Limitations

This is an early test release.  It is expected that users will find bugs and other
various instabilities.  The main goal of this release is to prove that the process can be
run and that users can demonstrate basic functionality as described in this document.
Nevertheless, the following are known issues with the 0.1 release:

 - Subscriber addresses are not always cleaned up after a consumer disconnects.  See
   <https://issues.apache.org/jira/browse/QPID-4964>.
 - Dispatch Router does not currently use the target address of a client's sender link to
   route messages.  It only looks at the "to" field in the message's headers.  See
   <https://issues.apache.org/jira/browse/QPID-5175>.
 - All subscription sources are treated as multicast addresses.  There is currently no
   facility for provisioning different types of addresses.  Multicast means that if there
   are multiple subscribers to the same address, they will all receive a copy of each
   message sent to that address.
 - SSL connectors and listeners are supported but very lightly (and not recently) tested.
 - SASL authentication is not currently integrated into any authentication framework.  Use
   ANONYMOUS for testing.

## Issues Addressed in this Release

 - [QPID-4612](https://issues.apache.org/jira/browse/QPID-4612) Dispatch - Change server and container pattern to be consistent with other objects
 - [QPID-4613](https://issues.apache.org/jira/browse/QPID-4613) Dispatch Message API Improvements
 - [QPID-4614](https://issues.apache.org/jira/browse/QPID-4614) CTEST for Dispatch
 - [QPID-4788](https://issues.apache.org/jira/browse/QPID-4788) Dispatch - Re-schedule of an "immediate" timer causes crash
 - [QPID-4816](https://issues.apache.org/jira/browse/QPID-4816) dispatch-router crashes when incomplete (but valid) url specified by client.
 - [QPID-4853](https://issues.apache.org/jira/browse/QPID-4853) Connectors are not closed when connections are closed cleanly
 - [QPID-4913](https://issues.apache.org/jira/browse/QPID-4913) Dispatch - Add a configuration file reader to configure the service
 - [QPID-4963](https://issues.apache.org/jira/browse/QPID-4963) Dispatch - Excessive latency in timers under light load
 - [QPID-4967](https://issues.apache.org/jira/browse/QPID-4967) Dispatch - Distributed routing protocol to compute paths across a network of routers
 - [QPID-4968](https://issues.apache.org/jira/browse/QPID-4968) Dispatch - Generalized framework for embedded Python modules
 - [QPID-4974](https://issues.apache.org/jira/browse/QPID-4974) Dispatch - Improve the API for parsing and composing AMQP-typed fields
 - [QPID-4997](https://issues.apache.org/jira/browse/QPID-4997) Dispatch - Thread safety issues in the usage of Proton
 - [QPID-5001](https://issues.apache.org/jira/browse/QPID-5001) Dispatch - A web page on the site for the Dispatch Router component
 - [QPID-5045](https://issues.apache.org/jira/browse/QPID-5045) Dispatch - Refactor the router data structures to allow both message-based and link-based routing that supports full link protocol
 - [QPID-5064](https://issues.apache.org/jira/browse/QPID-5064) Dispatch - make-install doesn't install the Python artifacts
 - [QPID-5066](https://issues.apache.org/jira/browse/QPID-5066) Dispatch - move Python code into the qpid.dispatch package
 - [QPID-5068](https://issues.apache.org/jira/browse/QPID-5068) Dispatch - Internal feature to easily add and update Delivery Annotations
 - [QPID-5096](https://issues.apache.org/jira/browse/QPID-5096) Dispatch - Install the configuration file
 - [QPID-5097](https://issues.apache.org/jira/browse/QPID-5097) Dispatch - create a source tarball
 - [QPID-5173](https://issues.apache.org/jira/browse/QPID-5173) [dispatch] cmake ignores overrides to CMAKE_INCLUDE_PATH and CMAKE_LIBRARY_PATH
 - [QPID-5181](https://issues.apache.org/jira/browse/QPID-5181) Dispatch - Assign temporary source addresses for dynamic listener links
 - [QPID-5185](https://issues.apache.org/jira/browse/QPID-5185) Move the qpid-dispatch.conf file to /etc/qpid
 - [QPID-5186](https://issues.apache.org/jira/browse/QPID-5186) Installing Dispatch should also install the LICENSE, TODO and related files
 - [QPID-5189](https://issues.apache.org/jira/browse/QPID-5189) Add a config.sh file for Qpid Dispatch to set an environment for running the router
 - [QPID-5201](https://issues.apache.org/jira/browse/QPID-5201) Dispatch - Fix build errors in Release mode
 - [QPID-5212](https://issues.apache.org/jira/browse/QPID-5212) Dispatch - Add management access to data in the router module
 - [QPID-5213](https://issues.apache.org/jira/browse/QPID-5213) Dispatch - Add a CLI tool to display manageable data in Dispatch
 - [QPID-5216](https://issues.apache.org/jira/browse/QPID-5216) Dispatch - Stabilization in anticipation of an early release
 - [QPID-5217](https://issues.apache.org/jira/browse/QPID-5217) Dispatch - Cleanup of API inconsistencies and oddities
 - [QPID-5218](https://issues.apache.org/jira/browse/QPID-5218) [dispatch] Crash when outgoing window > 0 and multiple subscribed Messenger clients
 - [QPID-5220](https://issues.apache.org/jira/browse/QPID-5220) Dispatch - Define Modes of Operation for the router function
 - [QPID-5221](https://issues.apache.org/jira/browse/QPID-5221) Dispatch - Configured connections can be annotated as to their role
 - [QPID-5257](https://issues.apache.org/jira/browse/QPID-5257) Dispatch - Move the code from trunk/qpid/extras to dispatch
 - [QPID-5258](https://issues.apache.org/jira/browse/QPID-5258) Dispatch - Prepare for Release 0.1
 - [QPID-5267](https://issues.apache.org/jira/browse/QPID-5267) Examples aren't being installed
 - [QPID-5310](https://issues.apache.org/jira/browse/QPID-5310) copy the correlationID into management replies
 - [QPID-5313](https://issues.apache.org/jira/browse/QPID-5313) qpid-dxrouterd binary should install the /usr/sbin on *nix
 - [QPID-5319](https://issues.apache.org/jira/browse/QPID-5319) Add ability to get list of connections through server management agent
 - [QPID-5335](https://issues.apache.org/jira/browse/QPID-5335) Dispatch Python libraries need to install to a private directory.
 - [QPID-5338](https://issues.apache.org/jira/browse/QPID-5338) The Dispatch top-level Python package should be renamed
 - [QPID-5339](https://issues.apache.org/jira/browse/QPID-5339) Dispatch - Intermittent crashes during scripted six-node tests
 - [QPID-5343](https://issues.apache.org/jira/browse/QPID-5343) Dispatch does not properly handle the drain protocol on senders.
 - [QPID-5350](https://issues.apache.org/jira/browse/QPID-5350) Dispatch - Management queries that receive empty tables results in corrupt response
 - [QPID-5351](https://issues.apache.org/jira/browse/QPID-5351) Settle on one prefix for Dispatch names
 - [QPID-5352](https://issues.apache.org/jira/browse/QPID-5352) Installation of python code ignores prefix
 - [QPID-5365](https://issues.apache.org/jira/browse/QPID-5365) Clean up file locations in Dispatch
 - [QPID-5367](https://issues.apache.org/jira/browse/QPID-5367) Dispatch - Add man pages and stubs for other documentation
 - [QPID-5380](https://issues.apache.org/jira/browse/QPID-5380) Dispatch - Simplify use of non-system instances
 - [QPID-5381](https://issues.apache.org/jira/browse/QPID-5381) Dispatch - Use dynamic source address for the reply-to in qdstat tool
 - [QPID-5392](https://issues.apache.org/jira/browse/QPID-5392) Dispatch - Remove ChangeLog; use jira and our website release pages instead
 - [QPID-5393](https://issues.apache.org/jira/browse/QPID-5393) Dispatch - Allow qdstat to query any router in the network from a connection
 - [QPID-5397](https://issues.apache.org/jira/browse/QPID-5397) Dispatch - Crash occurs when linked deliveries are concurrently settled
 - [QPID-5403](https://issues.apache.org/jira/browse/QPID-5403) Dispatch - The router-specific annotations have reserved keys
