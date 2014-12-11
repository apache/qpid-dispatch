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

import Queue, socket, time, threading
from . import ConnectionException, Endpoint, Handler, Message, Timeout, Url
from .reactors import AmqpSocket, Container, Events, SelectLoop, send_msg
from .handlers import ScopedHandler, IncomingMessageHandler

class BlockingLink(object):
    def __init__(self, connection, link):
        self.connection = connection
        self.link = link
        self.connection.wait(lambda: not (self.link.state & Endpoint.REMOTE_UNINIT),
                             msg="Opening link %s" % link.name)

    def close(self):
        self.connection.wait(not (self.link.state & Endpoint.REMOTE_ACTIVE),
                             msg="Closing link %s" % link.name)

    # Access to other link attributes.
    def __getattr__(self, name): return getattr(self.link, name)

class BlockingSender(BlockingLink):
    def __init__(self, connection, sender):
        super(BlockingSender, self).__init__(connection, sender)

    def send_msg(self, msg):
        delivery = send_msg(self.link, msg)
        self.connection.wait(lambda: delivery.settled, msg="Sending on sender %s" % self.link.name)

class BlockingReceiver(BlockingLink):
    def __init__(self, connection, receiver, credit=1):
        super(BlockingReceiver, self).__init__(connection, receiver)
        if credit: receiver.flow(credit)

class BlockingConnection(Handler):
    """
    A synchronous style connection wrapper.
    """
    def __init__(self, url, timeout=None, container=None):
        self.timeout = timeout
        self.container = container or Container()
        self.url = Url(url).defaults()
        self.conn = self.container.connect(url=self.url, handler=self)
        self.wait(lambda: not (self.conn.state & Endpoint.REMOTE_UNINIT),
                  msg="Opening connection")

    def create_sender(self, address, handler=None):
        return BlockingSender(self, self.container.create_sender(self.conn, address, handler=handler))

    def create_receiver(self, address, credit=1, dynamic=False, handler=None):
        return BlockingReceiver(
            self, self.container.create_receiver(self.conn, address, dynamic=dynamic, handler=handler), credit=credit)

    def close(self):
        self.conn.close()
        self.wait(lambda: not (self.conn.state & Endpoint.REMOTE_ACTIVE),
                  msg="Closing connection")

    def run(self):
        """ Hand control over to the event loop (e.g. if waiting indefinitely for incoming messages) """
        self.container.run()

    def wait(self, condition, timeout=False, msg=None):
        """Call do_work until condition() is true"""
        if timeout is False:
            timeout = self.timeout
        if timeout is None:
            while not condition():
                self.container.do_work()
        else:
            deadline = time.time() + timeout
            while not condition():
                if not self.container.do_work(deadline - time.time()):
                    txt = "Connection %s timed out" % self.url
                    if msg: txt += ": " + msg
                    raise Timeout(txt)

    def on_link_remote_close(self, event):
        if event.link.state & Endpoint.LOCAL_ACTIVE:
            self.closed(event.link.remote_condition)

    def on_connection_remote_close(self, event):
        if event.connection.state & Endpoint.LOCAL_ACTIVE:
            self.closed(event.connection.remote_condition)

    def on_disconnected(self, event):
        raise ConnectionException("Connection %s disconnected" % self.url);

    def closed(self, error=None):
        txt = "Connection %s closed" % self.url
        if error:
            txt += " due to: %s" % error
        else:
            txt += " by peer"
        raise ConnectionException(txt)


def atomic_count(start=0, step=1):
    """Thread-safe atomic count iterator"""
    lock = threading.Lock()
    count = start
    while True:
        with lock:
            count += step;
            yield count


class SyncRequestResponse(IncomingMessageHandler):
    """
    Implementation of the synchronous request-responce (aka RPC) pattern.
    Create an instance and call call(request) to send a request and wait for a response.
    """

    correlation_id = atomic_count()

    def __init__(self, connection, address=None):
        """
        @param connection: A L{BlockingConnection}
        @param address: Address for the sender.
            If this is not specified, then each request must have an address set.
        """
        super(SyncRequestResponse, self).__init__()
        self.connection = connection
        self.address = address
        self.sender = self.connection.create_sender(self.address)
        # dynamic=true generates a unique address dynamically for this receiver.
        # credit=1 because we want to receive 1 response message initially.
        self.receiver = self.connection.create_receiver(None, dynamic=True, credit=1, handler=self)
        self.response = None

    def call(self, request):
        """Send a request, wait for and return the response"""
        if not self.address and not request.address:
            raise ValueError("Request message has no address: %s" % request)
        request.reply_to = self.reply_to
        request.correlation_id = correlation_id = self.correlation_id.next()
        self.sender.send_msg(request)
        def wakeup():
            return self.response and (self.response.correlation_id == correlation_id)
        self.connection.wait(wakeup, msg="Waiting for response")
        response = self.response
        self.response = None    # Ready for next response.
        self.receiver.flow(1)   # Set up credit for the next response.
        return response

    @property
    def reply_to(self):
        """Return the dynamic address of our receiver."""
        return self.receiver.remote_source.address

    def on_message(self, event):
        """Called when we receive a message for our receiver."""
        self.response = event.message

    def close(self):
        self.connection.close()
