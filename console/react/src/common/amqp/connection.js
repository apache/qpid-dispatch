/*
Licensed to the Apache Software Foundation (ASF) under one
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
*/

import Correlator from "./correlator";
import rhea from "rhea";

class ConnectionManager {
  constructor(protocol) {
    this.rhea = rhea;
    this.sender = undefined;
    this.receiver = undefined;
    this.connection = undefined;
    this.version = undefined;
    this.errorText = undefined;
    this.protocol = protocol;
    this.schema = undefined;
    this.connectActions = [];
    this.disconnectActions = [];
    this.correlator = new Correlator();
    this.on_message = context => {
      this.correlator.resolve(context);
    };
    this.on_disconnected = context => {
      if (!context.connection.state.was_open) {
        return;
      }
      this.errorText = "Disconnected";
      this.executeDisconnectActions(this.errorText);
    };
    this.on_connection_open = () => {
      this.executeConnectActions();
    };
  }
  versionCheck = minVer => {
    var verparts = this.version.split(".");
    var minparts = minVer.split(".");
    try {
      for (var i = 0; i < minparts.length; ++i) {
        if (parseInt(minVer[i] > parseInt(verparts[i]))) return false;
      }
    } catch (e) {
      return false;
    }
    return true;
  };
  addConnectAction = action => {
    if (typeof action === "function") {
      this.delConnectAction(action);
      this.connectActions.push(action);
    }
  };
  addDisconnectAction = action => {
    if (typeof action === "function") {
      this.delDisconnectAction(action);
      this.disconnectActions.push(action);
    }
  };
  delConnectAction = action => {
    if (typeof action === "function") {
      var index = this.connectActions.indexOf(action);
      if (index >= 0) this.connectActions.splice(index, 1);
    }
  };
  delDisconnectAction = action => {
    if (typeof action === "function") {
      var index = this.disconnectActions.indexOf(action);
      if (index >= 0) this.disconnectActions.splice(index, 1);
    }
  };
  executeConnectActions = () => {
    this.connectActions.forEach(action => {
      try {
        action();
      } catch (e) {
        // in case the page that registered the handler has been unloaded
      }
    });
    this.connectActions = [];
  };
  executeDisconnectActions = message => {
    this.disconnectActions.forEach(action => {
      try {
        action(message);
      } catch (e) {
        // in case the page that registered the handler has been unloaded
      }
    });
    this.disconnectActions = [];
  };
  on = (eventType, fn) => {
    if (eventType === "connected") {
      this.addConnectAction(fn);
    } else if (eventType === "disconnected") {
      this.addDisconnectAction(fn);
    } else {
      console.log("unknown event type " + eventType);
    }
  };
  setSchema = schema => {
    this.schema = schema;
  };
  is_connected = () => {
    return (
      this.connection &&
      this.sender &&
      this.receiver &&
      this.receiver.remote &&
      this.receiver.remote.attach &&
      this.receiver.remote.attach.source &&
      this.receiver.remote.attach.source.address &&
      this.connection.is_open()
    );
  };
  disconnect = () => {
    if (this.sender) this.sender.close();
    if (this.receiver) this.receiver.close();
    if (this.connection) {
      this.connection.close();
      this.connection = null;
    }
  };
  /*
  restrict = (count, f) => {
    if (count) {
      var current = count;
      var reset;
      return successful_attempts => {
        if (reset !== successful_attempts) {
          current = count;
          reset = successful_attempts;
        }
        if (current--) return f(successful_attempts);
        else return -1;
      };
    } else {
      return f;
    }
  };

  backoff = (initial, max) => {
    var delay = initial;
    var reset;
    return successful_attempts => {
      if (reset !== successful_attempts) {
        delay = initial;
        reset = successful_attempts;
      }
      var current = delay;
      var next = delay * 2;
      delay = max > next ? next : max;
      return current;
    };
  };

  setReconnect = reconnect => {
    if (this.connection) {
      if (reconnect) {
        var initial = this.connection.get_option("initial_reconnect_delay", 100);
        var max = this.connection.get_option("max_reconnect_delay", 60000);
        this.connection.options.reconnect = this.restrict(
          this.connection.get_option("reconnect_limit"),
          this.backoff(initial, max)
        );
      } else {
        this.connection.options.reconnect = false;
      }
    }
  };
*/
  on_reconnected = () => {
    const self = this;
    this.connection.once("disconnected", this.on_disconnected);
    setTimeout(self.on_connection_open, 100);
  };

  createSenderReceiver = options => {
    return new Promise((resolve, reject) => {
      var timeout = options.timeout || 10000;
      // set a timer in case the setup takes too long
      var giveUp = () => {
        this.connection.removeListener("receiver_open", receiver_open);
        this.connection.removeListener("sendable", sendable);
        this.errorText = "timed out creating senders and receivers";
        reject(Error(this.errorText));
      };
      var timer = setTimeout(giveUp, timeout);
      // register an event hander for when the setup is complete
      var sendable = context => {
        clearTimeout(timer);
        this.version = this.connection.properties
          ? this.connection.properties.version
          : "0.1.0";
        // in case this connection dies
        //this.rhea.on("disconnected", this.on_disconnected);
        // in case this connection dies and is then reconnected automatically
        this.rhea.on("connection_open", this.on_reconnected);
        // receive messages here
        this.connection.on("message", this.on_message);
        resolve(context);
      };
      this.connection.once("sendable", sendable);
      // Now actually create the sender and receiver.
      // register an event handler for when the receiver opens
      var receiver_open = () => {
        // once the receiver is open, create the sender
        if (options.sender_address)
          this.sender = this.connection.open_sender(options.sender_address);
        else this.sender = this.connection.open_sender();
      };
      this.connection.once("receiver_open", receiver_open);
      // create a dynamic receiver
      this.receiver = this.connection.open_receiver({
        source: { dynamic: true },
      });
    });
  };

  connect = options => {
    this.options = options;
    return new Promise((resolve, reject) => {
      var finishConnecting = () => {
        this.createSenderReceiver(options).then(
          results => {
            this.on_connection_open();
            resolve(results);
          },
          error => {
            reject(error);
          }
        );
      };
      if (!this.is_connected()) {
        this.doConnect(options).then(
          () => {
            finishConnecting.call(this);
          },
          error => {
            // connect failed or timed out
            const message = error.message ? error.message : "";
            const condition = error.condition ? error.condition : "";
            this.errorText = `Unable to connect to ${options.address}:${options.port} ${message} ${condition}`;
            this.executeDisconnectActions(this.errorText);
            reject(Error(this.errorText));
          }
        );
      } else {
        console.log("called connect when already connected");
        finishConnecting.call(this);
      }
    });
  };
  getReceiverAddress = () => {
    return this.receiver.remote.attach.source.address;
  };

  doConnect = options => {
    return new Promise((resolve, reject) => {
      var timeout = options.timeout || 10000;
      //var reconnect = options.reconnect || false; // in case options.reconnect is undefined
      var reconnect = false;
      var baseAddress = options.address + ":" + options.port;
      if (options.linkRouteAddress) {
        baseAddress += "/" + options.linkRouteAddress;
      }
      var wsprotocol = window.location.protocol.startsWith("https") ? "wss" : "ws";
      var ws = this.rhea.websocket_connect(WebSocket);
      var c = {
        connection_details: new ws(wsprotocol + "://" + baseAddress, ["binary"]),
        reconnect: reconnect,
        properties: options.properties || {
          console_identifier: "Dispatch console",
        },
      };
      if (options.hostname) c.hostname = options.hostname;
      if (options.username && options.username !== "") {
        c.username = options.username;
      }
      if (options.password && options.password !== "") {
        c.password = options.password;
      }
      // set a timeout
      var timedOut = () => {
        clearTimeout(timer);
        //this.rhea.removeListener("disconnected", once_disconnected);
        this.rhea.removeListener("connection_open", connection_open);
        this.rhea.removeListener("error", connection_error);
        var rej = "failed to connect - timed out";
        reject(Error(rej));
      };
      var timer = setTimeout(timedOut, timeout);
      // the event handler for when the connection opens
      const connection_error = error => {
        clearTimeout(timer);
        this.rhea.removeListener("connection_open", connection_open);
        this.rhea.removeListener("error", connection_error);
        reject(error);
      };
      var connection_open = context => {
        clearTimeout(timer);
        this.rhea.removeListener("error", connection_error);
        if (options.reconnect) this.connection.set_reconnect(true);
        // get notified if this connection is disconnected
        this.connection.once("disconnected", this.on_disconnected);
        resolve({ context: context });
      };
      // register an event handler for when the connection opens
      this.rhea.once("connection_open", connection_open);
      this.rhea.on("error", connection_error);
      // attempt the connection
      this.connection = this.rhea.connect(c);
    });
  };
  sendMgmtQuery = (operation, to) => {
    to = to || "/$management";
    return this.send([], to, operation);
  };
  sendQuery = (toAddr, entity, attrs, operation) => {
    operation = operation || "QUERY";
    var fullAddr = this._fullAddr(toAddr);
    var body = { attributeNames: attrs || [] };
    return this.send(
      body,
      fullAddr,
      operation,
      this.schema.entityTypes[entity].fullyQualifiedType
    );
  };
  send = (body, to, operation, entityType) => {
    var application_properties = {
      operation: operation,
      type: "org.amqp.management",
      name: "self",
    };
    if (entityType) application_properties.entityType = entityType;
    return this._send(body, to, application_properties);
  };
  sendMethod = (toAddr, entity, attrs, operation, props) => {
    var fullAddr = this._fullAddr(toAddr);
    var application_properties = {
      operation: operation,
    };
    if (entity) {
      application_properties.type = this.schema.entityTypes[entity].fullyQualifiedType;
    }
    if (attrs.name) application_properties.name = attrs.name;
    else if (attrs.identity) application_properties.identity = attrs.identity;
    if (props) {
      for (var attrname in props) {
        application_properties[attrname] = props[attrname];
      }
    }
    return this._send(attrs, fullAddr, application_properties);
  };
  _send = (body, to, application_properties) => {
    if (!this.receiver.remote.attach) {
      // the connection was closed, but we had a pending send
      return;
    }
    var _correlationId = this.correlator.corr();
    var self = this;
    return new Promise((resolve, reject) => {
      try {
        self.correlator.register(_correlationId, resolve, reject);
        self.sender.send({
          body: body,
          to: to,
          reply_to: self.receiver.remote.attach.source.address,
          correlation_id: _correlationId,
          application_properties: application_properties,
        });
      } catch (error) {
        console.log(error);
      }
    });
  };
  _fullAddr = toAddr => {
    var toAddrParts = toAddr.split("/");
    toAddrParts.shift();
    var fullAddr = toAddrParts.join("/");
    return fullAddr;
  };
  availableQeueuDepth = () => {
    return this.correlator.depth();
  };
}

class ConnectionException {
  constructor(message) {
    this.message = message;
    this.name = "ConnectionException";
  }
}

const _ConnectionManager = ConnectionManager;
export { _ConnectionManager as ConnectionManager };
const _ConnectionException = ConnectionException;
export { _ConnectionException as ConnectionException };
