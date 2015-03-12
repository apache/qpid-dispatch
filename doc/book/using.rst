.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

Using Qpid Dispatch
===================

Configuration
-------------

The default configuration file is installed in
`install-prefix`/etc/qpid/qdrouterd.conf. This configuration file will
cause the router to run in standalone mode, listening on the standard
AMQP port (5672). Dispatch Router looks for the configuration file in
the installed location by default. If you wish to use a different path,
the "-c" command line option will instruct Dispatch Router as to which
configuration to load.

To run the router, invoke the executable: qdrouterd [-c my-config-file]

For more details of the configuration file see the `qdrouterd.conf(5)`
man page.

Client Compatibility
--------------------

Dispatch Router should, in theory, work with any client that is
compatible with AMQP 1.0. The following clients have been tested:

+-----------------+------------------------------------------------------------------+
| *Client*        | *Notes*                                                          |
+=================+==================================================================+
| qpid::messaging |The Qpid messaging clients work with Dispatch Router as long as   |
|                 |they are configured to use the 1.0 version of the protocol. To    |
|                 |enable AMQP 1.0 in the C++ client, use the {protocol:amqp1.0}     |
|                 |connection option.                                                |
|                 |                                                                  |
|                 |                                                                  |
|                 |                                                                  |
|                 |                                                                  |
|                 |                                                                  |
|                 |                                                                  |
|                 |                                                                  |
|                 |                                                                  |
|                 |                                                                  |
+-----------------+------------------------------------------------------------------+
| Proton Messenger| Messenger works with Dispatch Router.                            |
|                 |                                                                  |
|                 |                                                                  |
+-----------------+------------------------------------------------------------------+

Tools
-----

qdstat
~~~~~~

*qdstat* is a command line tool that lets you view the status of a
Dispatch Router. The following options are useful for seeing that the
router is doing:

+----------+-----------------------------------------------------------------------------+
| *Option* | *Description*                                                               |
+==========+=============================================================================+
| -l       |Print a list of AMQP links attached to the router. Links are                 |
|          |unidirectional. Outgoing links are usually associated with a subscription    |
|          |address. The tool distinguishes between *endpoint* links and *router*        |
|          |links. Endpoint links are attached to clients using the router. Router links |
|          |are attached to other routers in a network of routbers.                      |
|          |                                                                             |
+----------+-----------------------------------------------------------------------------+
| -a       |Print a list of addresses known to the router.                               |
+----------+-----------------------------------------------------------------------------+
| -n       |Print a list of known routers in the network.                                |
+----------+-----------------------------------------------------------------------------+
| -c       |Print a list of connections to the router.                                   |
+----------+-----------------------------------------------------------------------------+

For complete details see the `qdstat(8)` man page and the output of
`qdstat --help`.

qdmanage
~~~~~~~~

*qdmanage* is a general-purpose AMQP management client that allows you
to not only view but modify the configuration of a running dispatch
router.

For example you can query all the connection entities in the router::

   $ qdrouterd query --type connection

To enable logging debug and higher level messages by default::

   $ qdrouter update log/DEFAULT enable=debug+

In fact, everything that can be configured in the configuration file can
also be created in a running router via management. For example to
create a new listener in a running router::

   $ qdrouter create type=listener port=5555

Now you can connect to port 5555, for exampple::

   $ qdrouterd query -b localhost:5555 --type listener

For complete details see the `qdmanage(8)` man page and the output of
`qdmanage --help`. Also for details of what can be configured see the
`qdrouterd.conf(5)` man page.

Features and Examples
---------------------

Standalone and Interior Modes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The router can operate stand-alone or as a node in a network of routers.
The mode is configured in the *router* section of the configuration
file. In stand-alone mode, the router does not attempt to collaborate
with any other routers and only routes messages among directly connected
endpoints.

If your router is running in stand-alone mode, *qdstat -a* will look
like the following:

::

    $ qdstat -a
    Router Addresses
      class  address      in-proc  local  remote  in  out  thru  to-proc  from-proc
      ===============================================================================
      local  $management  Y        0      0       1   0    0     1        0
      local  temp.AY81ga           1      0       0   0    0     0        0

Note that there are two known addresses. *$management* is the address of
the router's embedded management agent. *temp.AY81ga* is the temporary
reply-to address of the *qdstat* client making requests to the agent.

If you change the mode to interior and restart the processs, the same
command will yield two additional addresses which are used for
inter-router communication:

::

    $ qdstat -a
    Router Addresses
      class  address      in-proc  local  remote  in  out  thru  to-proc  from-proc
      ===============================================================================
      local  $management  Y        0      0       1   0    0     1        0
      local  qdhello      Y        0      0       0   0    0     0        3
      local  qdrouter     Y        0      0       0   0    0     0        1
      local  temp.khOpGb           1      0       0   0    0     0        0

Mobile Subscribers
~~~~~~~~~~~~~~~~~~

The term "mobile subscriber" simply refers to the fact that a client may
connect to the router and subscribe to an address to receive messages
sent to that address. No matter where in the network the subscriber
attaches, the messages will be routed to the appropriate destination.

To illustrate a subscription on a stand-alone router, you can use the
examples that are provided with Qpid Proton. Using the *recv.py* example
receiver:

::

    $ recv.py amqp://0.0.0.0/my-address

This command creates a receiving link subscribed to the specified
address. To verify the subscription:

::

    $ qdstat -a
    Router Addresses
      class   address      in-proc  local  remote  in  out  thru  to-proc  from-proc
      ================================================================================
      local   $management  Y        0      0       1   0    0     1        0
      mobile  my-address            1      0       0   0    0     0        0
      local   temp.fDt8_a           1      0       0   0    0     0        0

You can then, in a separate command window, run a sender to produce
messages to that address:

::

    $ send.py -a amqp://0.0.0.0/my-address

Dynamic Reply-To
~~~~~~~~~~~~~~~~

Dynamic reply-to can be used to obtain a reply-to address that routes
back to a client's receiving link regardless of how many hops it has to
take to get there. To illustrate this feature, see below a simple
program (written in C++ against the qpid::messaging API) that queries
the management agent of the attached router for a list of other known
routers' management addresses.

::

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

The equivalent program written in Python against the Proton Messenger
API:

::

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
