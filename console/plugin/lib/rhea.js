require=(function e(t,n,r){function s(o,u){if(!n[o]){if(!t[o]){var a=typeof require=="function"&&require;if(!u&&a)return a(o,!0);if(i)return i(o,!0);var f=new Error("Cannot find module '"+o+"'");throw f.code="MODULE_NOT_FOUND",f}var l=n[o]={exports:{}};t[o][0].call(l.exports,function(e){var n=t[o][1][e];return s(n?n:e)},l,l.exports,e,t,n,r)}return n[o].exports}var i=typeof require=="function"&&require;for(var o=0;o<r.length;o++)s(r[o]);return s})({1:[function(require,module,exports){
(function (process,Buffer){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var frames = require('./frames.js');
var log = require('./log.js');
var sasl = require('./sasl.js');
var types = require('./types.js');
var util = require('./util.js');
var EndpointState = require('./endpoint.js');
var Session = require('./session.js');
var Transport = require('./transport.js');

var net = require("net");
var tls = require("tls");
var EventEmitter = require('events').EventEmitter;

var AMQP_PROTOCOL_ID = 0x00;
var TLS_PROTOCOL_ID = 0x02;

function get_socket_id(socket) {
    if (socket.get_id_string) return socket.get_id_string();
    return socket.localAddress + ':' + socket.localPort + ' -> ' + socket.remoteAddress + ':' + socket.remotePort;
};

function session_per_connection(conn) {
    var ssn = null;
    return {
        'get_session' : function () {
            if (!ssn) {
                ssn = conn.create_session();
                ssn.begin();
            }
            return ssn;
        }
    };
};

function restrict(count, f) {
    if (count) {
        var current = count;
        var reset;
        return function (successful_attempts) {
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
}

function backoff(initial, max) {
    var delay = initial;
    var reset;
    return function (successful_attempts) {
        if (reset !== successful_attempts) {
            delay = initial;
            reset = successful_attempts;
        }
        var current = delay;
        var next = delay*2;
        delay = max > next ? next : max;
        return current;
    };
}

function get_connect_fn(options) {
    if (options.transport === undefined || options.transport === 'tcp') {
        return net.connect;
    } else if (options.transport === 'tls' || options.transport === 'ssl') {
        return tls.connect;
    } else {
        throw Error('Unrecognised transport: ' + options.transport);
    }
}

function connection_details(options) {
    var details = {};
    details.connect = options.connect ? options.connect : get_connect_fn(options);
    details.host = options.host ? options.host : 'localhost';
    details.port = options.port ? options.port : 5672;
    details.options = options;
    return details;
};

var conn_counter = 1;

var Connection = function (options, container) {
    this.options = {};
    if (options) {
        for (var k in options) {
            this.options[k] = options[k];
        }
    }
    this.container = container;
    if (!this.options.id) {
        this.options.id = 'connection-' + conn_counter++;
    }
    if (!this.options.container_id) {
        this.options.container_id = container ? container.id : util.generate_uuid();
    }
    if (!this.options.connection_details) {
        var self = this;
        this.options.connection_details = function() { return connection_details(self.options); };
    }
    var reconnect = this.get_option('reconnect', true);
    if (typeof reconnect === 'boolean' && reconnect) {
        var initial = this.get_option('initial_reconnect_delay', 100);
        var max = this.get_option('max_reconnect_delay', 60000);
        this.options.reconnect = restrict(this.get_option('reconnect_limit'), backoff(initial, max));
    } else if (typeof reconnect === 'number') {
        var fixed = this.options.reconnect
        this.options.reconnect = restrict(this.get_option('reconnect_limit'), function () { return fixed; });
    }
    this.registered = false;
    this.state = new EndpointState();
    this.local_channel_map = {};
    this.remote_channel_map = {};
    this.local = {};
    this.remote = {};
    this.local.open = frames.open(this.options);
    this.local.close = frames.close({});
    this.session_policy = session_per_connection(this);
    this.amqp_transport = new Transport(this.options.id, AMQP_PROTOCOL_ID, frames.TYPE_AMQP, this);
    this.sasl_transport = undefined;
    this.transport = this.amqp_transport;
    this.conn_established_counter = 0;
    this.heartbeat_out = undefined;
    this.heartbeat_in = undefined;
    this.abort_idle = false;
    this.socket_ready = false;
};

Connection.prototype = Object.create(EventEmitter.prototype);
Connection.prototype.constructor = Connection;
Connection.prototype.dispatch = function(name, context) {
    log.events('Connection got event: ' + name);
    if (this.listeners(name).length) {
        EventEmitter.prototype.emit.apply(this, arguments);
        return true;
    } else if (this.container) {
        return this.container.dispatch.apply(this.container, arguments);
    }
};

Connection.prototype.reset = function() {
    if (this.abort_idle) {
        this.abort_idle = false;
        this.local.close.error = undefined;
        this.state = new EndpointState();
        this.state.open();
    }

    //reset transport
    this.amqp_transport = new Transport(this.options.id, AMQP_PROTOCOL_ID, frames.TYPE_AMQP, this);
    this.sasl_transport = undefined;
    this.transport = this.amqp_transport;

    //reset remote endpoint state
    this.state.disconnected();
    this.remote = {};
    //reset sessions:
    this.remote_channel_map = {};
    for (var k in this.local_channel_map) {
        this.local_channel_map[k].reset();
    }
    this.socket_ready = false;
}

Connection.prototype.connect = function () {
    this.is_server = false;
    this._connect(this.options.connection_details(this.conn_established_counter));
    this.open();
    return this;
};
Connection.prototype.reconnect = function () {
    log.reconnect('reconnecting...');
    this.reset();
    this._connect(this.options.connection_details(this.conn_established_counter));
    process.nextTick(this._process.bind(this));
    return this;
};

Connection.prototype._connect = function (details) {
    if (details.connect) {
        this.init(details.connect(details.port, details.host, details.options, this.connected.bind(this)));
    } else {
        this.init(get_connect_fn(details)(details.port, details.host, details.options, this.connected.bind(this)));
    }
    return this;
};

Connection.prototype.accept = function (socket) {
    this.is_server = true;
    log.io('[' + this.id + '] client accepted: '+ get_socket_id(socket));
    this.socket_ready = true;
    return this.init(socket);
};

Connection.prototype.init = function (socket) {
    this.socket = socket;
    this.socket.on('data', this.input.bind(this));
    this.socket.on('error', this.error.bind(this));
    this.socket.on('end', this.eof.bind(this));

    if (this.is_server) {
        var mechs;
        if (this.container && Object.getOwnPropertyNames(this.container.sasl_server_mechanisms).length) {
            mechs = this.container.sasl_server_mechanisms;
        }
        if (this.socket.encrypted && this.socket.authorized && this.get_option('enable_sasl_external', false)) {
            mechs = sasl.server_add_external(mechs ? util.clone(mechs) : {});
        }
        if (mechs) {
            this.sasl_transport = new sasl.Server(this, mechs);
        }
    } else {
        var mechanisms = this.get_option('sasl_mechanisms');
        if (!mechanisms) {
            var username = this.get_option('username');
            var password = this.get_option('password');
            if (username) {
                mechanisms = sasl.client_mechanisms();
                if (password) mechanisms.enable_plain(username, password);
                else mechanisms.enable_anonymous(username);
            }
        }
        if (this.socket.encrypted && this.options.cert && this.get_option('enable_sasl_external', false)) {
            if (!mechanisms) mechanisms = sasl.client_mechanisms();
            mechanisms.enable_external();
        }

        if (mechanisms) {
            this.sasl_transport = new sasl.Client(this, mechanisms);
        }
    }
    this.transport = this.sasl_transport ? this.sasl_transport : this.amqp_transport;
    return this;
};

Connection.prototype.attach_sender = function (options) {
    return this.session_policy.get_session().attach_sender(options);
};
Connection.prototype.open_sender = Connection.prototype.attach_sender;//alias

Connection.prototype.attach_receiver = function (options) {
    return this.session_policy.get_session().attach_receiver(options);
};
Connection.prototype.open_receiver = Connection.prototype.attach_receiver;//alias

Connection.prototype.get_option = function (name, default_value) {
    if (this.options[name] !== undefined) return this.options[name];
    else if (this.container) return this.container.get_option(name, default_value);
    else return default_value;
};

Connection.prototype.connected = function () {
    this.socket_ready = true;
    this.conn_established_counter++;
    log.io('[' + this.options.id + '] connected ' + get_socket_id(this.socket));
    this.output();
};
Connection.prototype.sasl_failed = function (text) {
    this.transport_error = {condition:'amqp:unauthorized-access', description:text};
    this._handle_error();
}

Connection.prototype._handle_error = function () {
    var error = this.get_error();
    if (error) {
        //TODO: invoke connection_close regardless of whether connection_error is handled
        //TODO: example for error handling
        if (!this.dispatch('connection_error', this._context())) {
            if (!this.dispatch('connection_close', this._context())) {
                console.log('error: ' + JSON.stringify(error));
            }
        }
        return true;
    } else {
        return false;
    }
}

Connection.prototype.get_error = function () {
    if (this.transport_error) return this.transport_error;
    if (this.remote.close && this.remote.close.error) return this.remote.close.error;
    return undefined;
}

Connection.prototype.output = function () {
    if (this.socket && this.socket_ready) {
        if (this.heartbeat_out) clearTimeout(this.heartbeat_out);
        this.transport.write(this.socket);
        if (((this.is_closed() && this.state.has_settled()) || this.abort_idle || this.transport_error) && !this.transport.has_writes_pending()) {
            this.socket.end();
        } else if (this.is_open() && this.remote.open.idle_time_out) {
            this.heartbeat_out = setTimeout(this._write_frame.bind(this), this.remote.open.idle_time_out / 2);
        }
    }
};

Connection.prototype.input = function (buff) {
    if (this.heartbeat_in) clearTimeout(this.heartbeat_in);
    log.io('[' + this.options.id + '] read ' + buff.length + ' bytes');
    var buffer;
    if (this.previous_input) {
        buffer = Buffer.concat([this.previous_input, buff], this.previous_input.length + buff.length);
        this.previous_input = null;
    } else {
        buffer = buff;
    }
    var read = this.transport.read(buffer, this);
    if (read < buffer.length) {
        this.previous_input = buffer.slice(read);
    }
    if (this.local.open.idle_time_out) this.heartbeat_in = setTimeout(this.idle.bind(this), this.local.open.idle_time_out);
    if (this.transport.has_writes_pending()) this.output();
};

Connection.prototype.idle = function () {
    if (this.is_open()) {
        this.abort_idle = true;
        this.local.close.error = {condition:'amqp:resource-limit-exceeded', description:'max idle time exceeded'};
        this.close();
    }
};

Connection.prototype.error = function (e) {
    console.log('[' + this.options.id + '] error: ' + e);
    this._disconnected();
};

Connection.prototype.eof = function (e) {
    this._disconnected();
};

Connection.prototype._disconnected = function () {
    if (!this.is_closed()) {
        if (!this.dispatch('disconnected', this._context())) {
            console.log('[' + this.options.id + '] disconnected');
        }
        if (!this.is_server && !this.transport_error && this.options.reconnect) {
            var delay = this.options.reconnect(this.conn_established_counter);
            if (delay >= 0) {
                log.reconnect('Scheduled reconnect in ' + delay + 'ms');
                setTimeout(this.reconnect.bind(this), delay);
            }
        }
    }
};

Connection.prototype.open = function () {
    if (this.state.open()) {
        this._register();
    }
};
Connection.prototype.close = function () {
    if (this.state.close()) {
        this._register();
    }
};

Connection.prototype.is_open = function () {
    return this.state.is_open();
};

Connection.prototype.is_closed = function () {
    return this.state.is_closed();
};

Connection.prototype.create_session = function () {
    var i = 0;
    while (this.local_channel_map[i]) i++;
    var session = new Session(this, i);
    this.local_channel_map[i] = session;
    return session;
}

Connection.prototype.on_open = function (frame) {
    if (this.state.remote_opened()) {
        this.remote.open = frame.performative;
        this.open();
        this.dispatch('connection_open', this._context());
    } else {
        throw Error('Open already received');
    }
};

Connection.prototype.on_close = function (frame) {
    if (this.state.remote_closed()) {
        this.remote.close = frame.performative;
        this.close();
        if (this.remote.close.error) {
            this._handle_error();
        } else {
            this.dispatch('connection_close', this._context());
        }
        if (this.heartbeat_out) clearTimeout(this.heartbeat_out);
    } else {
        throw Error('Close already received');
    }
};

Connection.prototype._register = function () {
    if (!this.registered) {
        this.registered = true;
        process.nextTick(this._process.bind(this));
    }
};

Connection.prototype._process = function () {
    this.registered = false;
    do {
        if (this.state.need_open()) {
            this._write_open();
        }
        for (var k in this.local_channel_map) {
            this.local_channel_map[k]._process();
        }
        if (this.state.need_close()) {
            this._write_close();
        }
    } while (!this.state.has_settled());
};

Connection.prototype._write_frame = function (channel, frame, payload) {
    this.amqp_transport.encode(frames.amqp_frame(channel, frame, payload));
    this.output();
};

Connection.prototype._write_open = function () {
    this._write_frame(0, this.local.open.described());
};

Connection.prototype._write_close = function () {
    this._write_frame(0, this.local.close.described());
};

Connection.prototype.on_begin = function (frame) {
    var session;
    if (frame.performative.remote_channel === null || frame.performative.remote_channel === undefined) {
        //peer initiated
        session = this.create_session();
        session.local.begin.remote_channel = frame.channel;
    } else {
        session = this.local_channel_map[frame.performative.remote_channel];
        if (!session) throw Error('Invalid value for remote channel ' + frame.performative.remote_channel);
    }
    session.on_begin(frame);
    this.remote_channel_map[frame.channel] = session;
};

Connection.prototype.get_peer_certificate = function() {
    if (this.socket && this.socket.getPeerCertificate) {
        return this.socket.getPeerCertificate();
    } else {
        return undefined;
    }
};

Connection.prototype._context = function (c) {
    var context = c ? c : {};
    context.connection = this;
    if (this.container) context.container = this.container;
    return context;
};

function delegate_to_session(name) {
    Connection.prototype['on_' + name] = function (frame) {
        var session = this.remote_channel_map[frame.channel];
        if (!session) {
            throw Error(name + ' received on invalid channel ' + frame.channel);
        }
        session['on_' + name](frame);
    };
};

delegate_to_session('end');
delegate_to_session('attach');
delegate_to_session('detach');
delegate_to_session('transfer');
delegate_to_session('disposition');
delegate_to_session('flow');

module.exports = Connection

}).call(this,require('_process'),require("buffer").Buffer)
},{"./endpoint.js":2,"./frames.js":3,"./log.js":5,"./sasl.js":8,"./session.js":9,"./transport.js":11,"./types.js":12,"./util.js":13,"_process":24,"buffer":19,"events":23,"net":18,"tls":18}],2:[function(require,module,exports){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var EndpointState = function () {
    this.init();
};

EndpointState.prototype.init = function () {
    this.local_open = false;
    this.remote_open = false;
    this.open_requests = 0;
    this.close_requests = 0;
    this.initialised = false;
};

EndpointState.prototype.open = function () {
    this.initialised = true;
    if (!this.local_open) {
        this.local_open = true;
        this.open_requests++;
        return true;
    } else {
        return false;
    }
};

EndpointState.prototype.close = function () {
    if (this.local_open) {
        this.local_open = false;
        this.close_requests++;
        return true;
    } else {
        return false;
    }
};

EndpointState.prototype.disconnected = function () {
    var was_open = this.local_open;
    this.init();
    if (was_open) {
        this.open();
    } else {
        this.close();
    }
};

EndpointState.prototype.remote_opened = function (frame) {
    if (!this.remote_open) {
        this.remote_open = true;
        return true;
    } else {
        return false;
    }
};

EndpointState.prototype.remote_closed = function (frame) {
    if (this.remote_open) {
        this.remote_open = false;
        return true;
    } else {
        return false;
    }
};

EndpointState.prototype.is_open = function () {
    return this.local_open && this.remote_open;
};

EndpointState.prototype.is_closed = function () {
    return this.initialised && !this.local_open && !this.remote_open;
};

EndpointState.prototype.has_settled = function () {
    return this.open_requests == 0 && this.close_requests == 0;
};

EndpointState.prototype.need_open = function () {
    if (this.open_requests > 0) {
        this.open_requests--;
        return true;
    } else {
        return false;
    }
};

EndpointState.prototype.need_close = function () {
    if (this.close_requests > 0) {
        this.close_requests--;
        return true;
    } else {
        return false;
    }
};

module.exports = EndpointState

},{}],3:[function(require,module,exports){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var types = require('./types.js');

var frames = {};
var by_descriptor = {};

frames.read_header = function(buffer) {
    var offset = 4;
    var header = {};
    var name = buffer.toString('ascii', 0, offset);
    if (name !== 'AMQP') {
        throw Error('Invalid protocol header for AMQP ' + name);
    }
    header.protocol_id = buffer.readUInt8(offset++);
    header.major = buffer.readUInt8(offset++);
    header.minor = buffer.readUInt8(offset++);
    header.revision = buffer.readUInt8(offset++);
    if (header.major !== 1 || header.minor !== 0) {
        throw Error('Unsupported AMQP version: ' + JSON.stringify(header));
    }
    return header;
};
frames.write_header = function(buffer, header) {
    var offset = 4;
    buffer.write('AMQP', 0, offset, 'ascii');
    buffer.writeUInt8(header.protocol_id, offset++);
    buffer.writeUInt8(header.major, offset++);
    buffer.writeUInt8(header.minor, offset++);
    buffer.writeUInt8(header.revision, offset++);
    return 8;
};
//todo: define enumeration for frame types
frames.TYPE_AMQP = 0x00;
frames.TYPE_SASL = 0x01;

frames.read_frame = function(buffer) {
    var reader = new types.Reader(buffer);
    var frame = {};
    frame.size = reader.read_uint(4);
    if (reader.remaining < frame.size) {
        return null;
    }
    var doff = reader.read_uint(1);
    if (doff < 2) {
        throw Error('Invalid data offset, must be at least 2 was ' + doff);
    }
    frame.type = reader.read_uint(1);
    if (frame.type === frames.TYPE_AMQP) {
        frame.channel = reader.read_uint(2);
    } else if (frame.type === frames.TYPE_SASL) {
        reader.skip(2);
    } else {
        throw Error('Unknown frame type ' + frame.type);
    }
    if (doff > 1) {
        //ignore any extended header
        reader.skip(doff * 4 - 8);
    }
    if (reader.remaining()) {
        frame.performative = reader.read();
        var c = by_descriptor[frame.performative.descriptor.value];
        if (c) {
            frame.performative = new c(frame.performative.value);
        }
        if (reader.remaining()) {
            frame.payload = reader.read_bytes(reader.remaining());
        }
    }
    return frame;
};

frames.write_frame = function(frame) {
    var writer = new types.Writer();
    writer.skip(4);//skip size until we know how much we have written
    writer.write_uint(2, 1);//doff
    writer.write_uint(frame.type, 1);
    if (frame.type === frames.TYPE_AMQP) {
        writer.write_uint(frame.channel, 2);
    } else if (frame.type === frames.TYPE_SASL) {
        writer.write_uint(0, 2);
    } else {
        throw Error('Unknown frame type ' + frame.type);
    }
    if (frame.performative) {
        writer.write(frame.performative);
        if (frame.payload) {
            writer.write_bytes(frame.payload);
        }
    }
    var buffer = writer.toBuffer();
    buffer.writeUInt32BE(buffer.length, 0);//fill in the size
    return buffer;
};

frames.amqp_frame = function(channel, performative, payload) {
    return {'channel': channel || 0, 'type': frames.TYPE_AMQP, 'performative': performative, 'payload': payload};
};
frames.sasl_frame = function(performative) {
    return {'channel': 0, 'type': frames.TYPE_SASL, 'performative': performative};
};

function define_frame(type, def) {
    var c = types.define_composite(def);
    frames[def.name] = c.create;
    by_descriptor[Number(c.descriptor.numeric).toString(10)] = c;
    by_descriptor[c.descriptor.symbolic] = c;
};

var open = {name: "open",
            code: 0x10,
            fields: [
                 {name:"container_id", type:"string", mandatory:true},
                 {name:"hostname", type:"string"},
                 {name:"max_frame_size", type:"uint", default_value:4294967295},
                 {name:"channel_max", type:"ushort", default_value:65535},
                 {name:"idle_time_out", type:"uint"},
                 {name:"outgoing_locales", type:"symbol", multiple:true},
                 {name:"incoming_locales", type:"symbol", multiple:true},
                 {name:"offered_capabilities", type:"symbol", multiple:true},
                 {name:"desired_capabilities", type:"symbol", multiple:true},
                 {name:"properties", type:"symbolic_map"}
             ]
           };

var begin = {name:"begin",
             code:0x11,
             fields:[
                 {name:"remote_channel", type:"ushort"},
                 {name:"next_outgoing_id", type:"uint", mandatory:true},
                 {name:"incoming_window", type:"uint", mandatory:true},
                 {name:"outgoing_window", type:"uint", mandatory:true},
                 {name:"handle_max", type:"uint", default_value:"4294967295"},
                 {name:"offered_capabilities", type:"symbol", multiple:true},
                 {name:"desired_capabilities", type:"symbol", multiple:true},
                 {name:"properties", type:"symbolic_map"}
             ]
            };

var attach = {name:"attach",
              code:0x12,
              fields:[
                  {name:"name", type:"string", mandatory:true},
                  {name:"handle", type:"uint", mandatory:true},
                  {name:"role", type:"boolean", mandatory:true},
                  {name:"snd_settle_mode", type:"ubyte", default_value:2},
                  {name:"rcv_settle_mode", type:"ubyte", default_value:0},
                  {name:"source", type:"*"},
                  {name:"target", type:"*"},
                  {name:"unsettled", type:"map"},
                  {name:"incomplete_unsettled", type:"boolean", default_value:false},
                  {name:"initial_delivery_count", type:"uint"},
                  {name:"max_message_size", type:"ulong"},
                  {name:"offered_capabilities", type:"symbol", multiple:true},
                  {name:"desired_capabilities", type:"symbol", multiple:true},
                  {name:"properties", type:"symbolic_map"}
              ]
             };

var flow = {name:"flow",
            code:0x13,
            fields:[
                {name:"next_incoming_id", type:"uint"},
                {name:"incoming_window", type:"uint", mandatory:true},
                {name:"next_outgoing_id", type:"uint", mandatory:true},
                {name:"outgoing_window", type:"uint", mandatory:true},
                {name:"handle", type:"uint"},
                {name:"delivery_count", type:"uint"},
                {name:"link_credit", type:"uint"},
                {name:"available", type:"uint"},
                {name:"drain", type:"boolean", default_value:false},
                {name:"echo", type:"boolean", default_value:false},
                {name:"properties", type:"symbolic_map"}
            ]
           };

var transfer = {name:"transfer",
                code:0x14,
                fields:[
                    {name:"handle", type:"uint", mandatory:true},
                    {name:"delivery_id", type:"uint"},
                    {name:"delivery_tag", type:"binary"},
                    {name:"message_format", type:"uint"},
                    {name:"settled", type:"boolean"},
                    {name:"more", type:"boolean", default_value:false},
                    {name:"rcv_settle_mode", type:"ubyte"},
                    {name:"state", type:"delivery_state"},
                    {name:"resume", type:"boolean", default_value:false},
                    {name:"aborted", type:"boolean", default_value:false},
                    {name:"batchable", type:"boolean", default_value:false}
                ]
               };

var disposition = {name:"disposition",
                   code:0x15,
                   fields:[
                       {name:"role", type:"boolean", mandatory:true},
                       {name:"first", type:"uint", mandatory:true},
                       {name:"last", type:"uint"},
                       {name:"settled", type:"boolean", default_value:false},
                       {name:"state", type:"*"},
                       {name:"batchable", type:"boolean", default_value:false}
                   ]
                  };

var detach = {name: "detach",
             code: 0x16,
              fields: [
                  {name:"handle", type:"uint", mandatory:true},
                  {name:"closed", type:"boolean", default_value:false},
                  {name:"error", type:"error"}
              ]
             };

var end = {name: "end",
             code: 0x17,
             fields: [
                 {name:"error", type:"error"}
             ]
            };

var close = {name: "close",
             code: 0x18,
             fields: [
                 {name:"error", type:"error"}
             ]
            };

define_frame(frames.TYPE_AMQP, open);
define_frame(frames.TYPE_AMQP, begin);
define_frame(frames.TYPE_AMQP, attach);
define_frame(frames.TYPE_AMQP, flow);
define_frame(frames.TYPE_AMQP, transfer);
define_frame(frames.TYPE_AMQP, disposition);
define_frame(frames.TYPE_AMQP, detach);
define_frame(frames.TYPE_AMQP, end);
define_frame(frames.TYPE_AMQP, close);

var sasl_mechanisms = {name:"sasl_mechanisms", code:0x40,
                       fields: [
                           {name:"sasl_server_mechanisms", type:"symbol", multiple:true, mandatory:true}
                       ]};

var sasl_init = {name:"sasl_init", code:0x41,
                 fields: [
                     {name:"mechanism", type:"symbol", mandatory:true},
                     {name:"initial_response", type:"binary"},
                     {name:"hostname", type:"string"}
                 ]};

var sasl_challenge = {name:"sasl_challenge", code:0x42,
                      fields: [
                          {name:"challenge", type:"binary", mandatory:true}
                      ]};

var sasl_response = {name:"sasl_response", code:0x43,
                     fields: [
                         {name:"response", type:"binary", mandatory:true}
                     ]};

var sasl_outcome = {name:"sasl_outcome", code:0x44,
                    fields: [
                        {name:"code", type:"ubyte", mandatory:true},
                        {name:"additional_data", type:"binary"}
                    ]};

define_frame(frames.TYPE_SASL, sasl_mechanisms);
define_frame(frames.TYPE_SASL, sasl_init);
define_frame(frames.TYPE_SASL, sasl_challenge);
define_frame(frames.TYPE_SASL, sasl_response);
define_frame(frames.TYPE_SASL, sasl_outcome);

module.exports = frames;

},{"./types.js":12}],4:[function(require,module,exports){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var frames = require('./frames.js');
var log = require('./log.js');
var message = require('./message.js');
var terminus = require('./terminus.js')
var types = require('./types.js')
var EndpointState = require('./endpoint.js');

var FlowController = function (window) {
    this.window = window;
};
FlowController.prototype.update = function (context) {
    var delta = this.window - context.receiver.credit;
    context.receiver.flow(delta);
};

function auto_settle(context) {
    context.delivery.settled = true;
};

function auto_accept(context) {
    context.delivery.update(true, message.accepted().described());
};

var EventEmitter = require('events').EventEmitter;

var link = Object.create(EventEmitter.prototype);
link.dispatch = function(name, context) {
    log.events('Link got event: '+ name);
    EventEmitter.prototype.emit.apply(this.observers, arguments);
    if (this.listeners(name).length) {
        EventEmitter.prototype.emit.apply(this, arguments);
    } else {
        this.session.dispatch.apply(this.session, arguments);
    }
};
link.set_source = function (fields) {
    this.local.attach.source = terminus.source(fields).described();
};
link.set_target = function (fields) {
    this.local.attach.target = terminus.target(fields).described();
};

link.attach = function () {
    if (this.state.open()) {
        this.connection._register();
    }
};
link.open = link.attach;

link.detach = function () {
    this.local.detach.closed = false;
    if (this.state.close()) {
        this.connection._register();
    }
};
link.close = function() {
    this.local.detach.closed = true;
    if (this.state.close()) {
        this.connection._register();
    }
}

link.is_open = function () {
    return this.session.is_open() && this.state.is_open();
};

link.is_closed = function () {
    return this.session.is_closed() || this.state.is_closed();
};

link._process = function () {
    do {
        if (this.state.need_open()) {
            this.session.output(this.local.attach.described());
        }

        if (this.issue_flow) {
            this.session._write_flow(this);
            this.issue_flow = false;
        }

        if (this.state.need_close()) {
            this.session.output(this.local.detach.described());
        }
    } while (!this.state.has_settled());
};

link.on_attach = function (frame) {
    if (this.state.remote_opened()) {
        if (!this.remote.handle) {
            this.remote.handle = frame.handle;
        }
        frame.performative.source = terminus.unwrap(frame.performative.source);
        frame.performative.target = terminus.unwrap(frame.performative.target);
        this.remote.attach = frame.performative;
        this.open();
        this.dispatch(this.is_receiver() ? 'receiver_open' : 'sender_open', this._context());
    } else {
        throw Error('Attach already received');
    }
};

link.on_detach = function (frame) {
    if (this.state.remote_closed()) {
        this.remote.detach = frame.performative;
        this.close();
        this.dispatch(this.local.attach.role ? 'receiver_close' : 'sender_close', this._context());
    } else {
        throw Error('Detach already received');
    }
};

function is_internal(name) {
    switch (name) {
    case 'handle':
    case 'role':
    case 'initial_delivery_count':
        return true;
    default:
        return false;
    }
}

link.init = function (session, name, local_handle, opts, is_receiver) {
    this.session = session;
    this.connection = session.connection;
    this.name = name;
    this.options = opts === undefined ? {} : opts;
    this.state = new EndpointState();
    this.issue_flow = false;//currently only used by receiver
    this.local = {'handle': local_handle};
    this.local.attach = frames.attach({'handle':local_handle,'name':name, role:is_receiver});
    for (var f in this.local.attach) {
        if (!is_internal(f) && this.options[f] !== undefined) {
            this.local.attach[f] = this.options[f];
        }
    }
    this.local.detach = frames.detach({'handle':local_handle, 'closed':true});
    this.remote = {'handle':undefined};
    this.delivery_count = 0;
    this.credit = 0;
    this.observers = new EventEmitter();
};
link.reset = function() {
    this.state.disconnected();
    this.remote = {'handle':undefined};
    this.delivery_count = 0;
    this.credit = 0;
};

link.has_credit = function () {
    return this.credit > 0;
};
link.is_receiver = function () {
    return this.local.attach.role;
};
link._context = function (c) {
    var context = c ? c : {};
    if (this.is_receiver()) {
        context.receiver = this;
    } else {
        context.sender = this;
    }
    return this.session._context(context);
};
link.get_option = function (name, default_value) {
    if (this.options[name] !== undefined) return this.options[name];
    else return this.session.get_option(name, default_value);
};

var Sender = function (session, name, local_handle, opts) {
    this.init(session, name, local_handle, opts, false);
    this.local.attach.initial_delivery_count = 0;
    this.tag = 0;
    if (this.get_option('autosettle', true)) {
        this.observers.on('settled', auto_settle);
    }
};
Sender.prototype = Object.create(link);
Sender.prototype.constructor = Sender;
Sender.prototype.next_tag = function () {
    return new String(this.tag++);
};
Sender.prototype.sendable = function (frame) {
    return this.credit && this.session.outgoing.available();
}
Sender.prototype.on_flow = function (frame) {
    var flow = frame.performative;
    this.credit = flow.delivery_count + flow.link_credit - this.delivery_count;
    if (this.is_open()) {
        this.dispatch('sender_flow', this._context());
        if (this.sendable()) {
            this.dispatch('sendable', this._context());
        }
    }
};
Sender.prototype.on_transfer = function (frame) {
    throw Error('got transfer on sending link');
};
Sender.prototype.send = function (msg, tag) {
    return this.session.send(this, tag ? tag : this.next_tag(), message.encode(msg), 0);
};


var Receiver = function (session, name, local_handle, opts) {
    this.init(session, name, local_handle, opts, true);
    this.set_prefetch(this.get_option('prefetch', 100));
    if (this.get_option('autoaccept', true)) {
        this.observers.on('message', auto_accept);
    }
};
Receiver.prototype = Object.create(link);
Receiver.prototype.constructor = Receiver;
Receiver.prototype.on_flow = function (frame) {
    this.dispatch('receiver_flow', this._context());
};
Receiver.prototype.flow = function(credit) {
    if (credit > 0) {
        this.credit += credit;
        this.issue_flow = true;
        this.connection._register();
    }
};

Receiver.prototype.set_prefetch = function(prefetch) {
    if (prefetch > 0) {
        var flow_controller = new FlowController(prefetch);
        var listener = flow_controller.update.bind(flow_controller);
        this.observers.on('message', listener);
        this.observers.on('receiver_open', listener);
    }
}

module.exports = {'Sender': Sender, 'Receiver':Receiver};

},{"./endpoint.js":2,"./frames.js":3,"./log.js":5,"./message.js":6,"./terminus.js":10,"./types.js":12,"events":23}],5:[function(require,module,exports){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var debug = require('debug');

module.exports = {
    'frames' : debug('rhea:frames'),
    'raw' : debug('rhea:raw'),
    'reconnect' : debug('rhea:reconnect'),
    'events' : debug('rhea:events'),
    'message' : debug('rhea:message'),
    'flow' : debug('rhea:flow'),
    'io' : debug('rhea:io')
}

},{"debug":15}],6:[function(require,module,exports){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var log = require('./log.js');
var types = require('./types.js');

var by_descriptor = {};
var unwrappers = {};
var wrappers = [];
var message = {};

function define_section(descriptor, unwrap, wrap) {
    unwrap.descriptor = descriptor;
    unwrappers[descriptor.symbolic] = unwrap;
    unwrappers[Number(descriptor.numeric).toString(10)] = unwrap;
    if (wrap) {
        wrappers.push(wrap);
    }
};

function define_composite_section(def) {
    var c = types.define_composite(def);
    message[def.name] = c.create;
    by_descriptor[Number(c.descriptor.numeric).toString(10)] = c;
    by_descriptor[c.descriptor.symbolic] = c;

    var unwrap = function (msg, section) {
        msg[def.name] = new c(section.value);
    };

    var wrap = function (sections, msg) {
        if (msg[def.name]) {
            if (msg[def.name].described) {
                sections.push(msg[def.name].described());
            } else {
                sections.push(c.create(msg[def.name]).described());
            }
        }
    };
    define_section(c.descriptor, unwrap, wrap);
};


function define_map_section(def) {
    var descriptor = {numeric:def.code};
    descriptor.symbolic = 'amqp:' + def.name.replace(/_/g, '-') + ':map';
    var unwrap = function (msg, section) {
        msg[def.name] = types.unwrap(section);
    };
    var wrap = function (sections, msg) {
        if (msg[def.name]) {
            sections.push(types.described(types.wrap_ulong(descriptor.numeric), types.wrap_map(msg[def.name])));
        }
    };
    define_section(descriptor, unwrap, wrap);
};

define_composite_section({name:"header",
                          code:0x70,
                          fields:[
                              {name:"durable", type:"boolean", default_value:false},
                              {name:"priority", type:"ubyte", default_value:4},
                              {name:"ttl", type:"uint"},
                              {name:"first_acquirer", type:"boolean", default_value:false},
                              {name:"delivery_count", type:"uint", default_value:0}
                          ]
                         });
define_map_section({name:"delivery_annotations", code:0x71});
define_map_section({name:"message_annotations", code:0x72});
define_composite_section({name:"properties",
                          code:0x73,
                          fields:[
                              {name:"message_id", type:"message_id"},
                              {name:"user_id", type:"binary"},
                              {name:"to", type:"string"},
                              {name:"subject", type:"string"},
                              {name:"reply_to", type:"string"},
                              {name:"correlation_id", type:"message_id"},
                              {name:"content_type", type:"symbol"},
                              {name:"content_encoding", type:"symbol"},
                              {name:"absolute_expiry_time", type:"timestamp"},
                              {name:"creation_time", type:"timestamp"},
                              {name:"group_id", type:"string"},
                              {name:"group_sequence", type:"uint"},
                              {name:"reply_to_group_id", type:"string"}
                          ]
                         });
define_map_section({name:"application_properties", code:0x74});

define_section({numeric:0x77, symbolic:'amqp:value:*'},
               function(msg, section) { msg.body = types.unwrap(section); },
               function(sections, msg) { sections.push(types.described(types.wrap_ulong(0x77), types.wrap(msg.body))); });

define_map_section({name:"footer", code:0x78});

message.encode = function(obj) {
    var sections = [];

    wrappers.forEach(function (wrapper_fn) { wrapper_fn(sections, obj); });
    var writer = new types.Writer();
    for (var i = 0; i < sections.length; i++) {
        log.message('Encoding section ' + (i+1) + ' of ' + sections.length + ': ' + sections[i]);
        writer.write(sections[i]);
    }
    var data = writer.toBuffer();
    log.message('encoded ' + data.length + ' bytes');
    return data;
}

message.decode = function(buffer) {
    var msg = {};
    var reader = new types.Reader(buffer);
    while (reader.remaining()) {
        var s = reader.read();
        log.message('decoding section: ' + JSON.stringify(s) + ' of type: ' + JSON.stringify(s.descriptor));
        if (s.descriptor) {
            var unwrap = unwrappers[s.descriptor.value];
            if (unwrap) {
                unwrap(msg, s);
            } else {
                console.log("WARNING: did not recognise message section with descriptor " + s.descriptor);
            }
        } else {
            console.log("WARNING: expected described message section got " + JSON.stringify(s));
        }
    }
    return msg;
}

var outcomes = {};

function define_outcome(def) {
    var c = types.define_composite(def);
    c.composite_type = def.name;
    message[def.name] = c.create;
    outcomes[Number(c.descriptor.numeric).toString(10)] = c;
    outcomes[c.descriptor.symbolic] = c;
    message['is_' + def.name] = function (o) {
        if (o && o.descriptor) {
            var c = outcomes[o.descriptor.value];
            if (c) {
                return c.descriptor.numeric == def.code;
            }
        }
        return false;
    };
}

message.unwrap_outcome = function (outcome) {
    if (outcome && outcome.descriptor) {
        var c = outcomes[outcome.descriptor.value];
        if (c) {
            return new c(outcome);
        }
    }
    console.log('unrecognised outcome');
    return outcome;
};

message.are_outcomes_equivalent = function(a, b) {
    if (a === undefined && b === undefined) return true;
    else if (a === undefined || b === undefined) return false;
    else return a.descriptor.value == b.descriptor.value && JSON.stringify(a) == JSON.stringify(b);
};

define_outcome({name:"received", code:0x23,
                fields:[
                    {name:"section-number", type:"uint", mandatory:true},
                    {name:"section-offset", type:"ulong", mandatory:true}
                ]});
define_outcome({name:"accepted", code:0x24, fields:[]});
define_outcome({name:"rejected", code:0x25, fields:[{name:"error", type:"error"}]});
define_outcome({name:"released", code:0x26, fields:[]});
define_outcome({name:"modified",
 code:0x27,
 fields:[
     {name:"delivery-failed", type:"boolean"},
     {name:"undeliverable-here", type:"boolean"},
     {name:"message-annotations", type:"fields"}
]});

module.exports = message;

},{"./log.js":5,"./types.js":12}],7:[function(require,module,exports){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var url = require('url');

var simple_id_generator = {
    counter : 1,
    next : function() {
        return this.counter++;
    }
};

var Client = function (container, address) {
    var u = url.parse(address);
    //TODO: handle scheme and user/password if present
    this.connection = container.connect({'host':u.hostname, 'port':u.port});
    this.connection.on('message', this._response.bind(this));
    this.connection.on('receiver_open', this._ready.bind(this));
    this.sender = this.connection.attach_sender(u.path.substr(1));
    this.receiver = this.connection.attach_receiver({source:{dynamic:true}});
    this.id_generator = simple_id_generator;
    this.pending = [];//requests yet to be made (waiting for receiver to open)
    this.outstanding = {};//requests sent, for which responses have not yet been received
};

Client.prototype._request = function (id, name, args, callback) {
    var request = {properties:{}};
    request.properties.subject = name;
    request.body = args;
    request.properties.message_id = id;
    request.properties.reply_to = this.receiver.remote.attach.source.address;
    this.outstanding[id] = callback;
    this.sender.send(request);
};

Client.prototype._response = function (context) {
    var id = context.message.properties.correlation_id;
    var callback = this.outstanding[id];
    if (callback) {
        if (context.message.properties.subject === 'ok') {
            callback(context.message.body);
        } else {
            callback(undefined, {name: context.message.properties.subject, description: context.message.body});
        }
    } else {
        console.log('no request pending for ' + id + ', ignoring response');
    }
};

Client.prototype._ready = function (context) {
    this._process_pending();
};

Client.prototype._process_pending = function () {
    for (var i = 0; i < this.pending.length; i++) {
        var r = this.pending[i];
        this._request(r.id, r.name, r.args, r.callback);
    }
    this.pending = [];
};

Client.prototype.call = function (name, args, callback) {
    var id = this.id_generator.next();
    if (this.receiver.is_open() && this.pending.length === 0) {
        this._request(id, name, args, callback);
    } else {
        //need to wait for reply-to address
        this.pending.push({'name':name, 'args':args, 'callback':callback, 'id':id});
    }
};

Client.prototype.close = function () {
    this.receiver.close();
    this.sender.close();
    this.connection.close();
};

Client.prototype.define = function (name) {
    this[name] = function (args, callback) { this.call(name, args, callback); };
};

var Cache = function (ttl, purged) {
    this.ttl = ttl;
    this.purged = purged;
    this.entries = {};
    this.timeout = undefined;
};

Cache.prototype.clear = function () {
    if (this.timeout) clearTimeout(this.timeout);
    this.entries = {};
}

Cache.prototype.put = function (key, value) {
    this.entries[key] = {'value':value, 'last_accessed': Date.now()};
    if (!this.timeout) this.timeout = setTimeout(this.purge.bind(this), this.ttl);
};

Cache.prototype.get = function (key) {
    var entry = this.entries[key];
    if (entry) {
        entry.last_accessed = Date.now();
        return entry.value;
    } else {
        return undefined;
    }
};

Cache.prototype.purge = function() {
    //TODO: this could be optimised if the map is large
    var now = Date.now();
    var expired = [];
    var live = 0;
    for (var k in this.entries) {
        if (now - this.entries[k].last_accessed >= this.ttl) {
            expired.push(k);
        } else {
            live++;
        }
    }
    for (var i = 0; i < expired.length; i++) {
        var entry = this.entries[expired[i]];
        delete this.entries[expired[i]];
        this.purged(entry.value);
    }
    if (live && !this.timeout) {
        this.timeout = setTimeout(this.purge.bind(this), this.ttl);
    }
};

var LinkCache = function (factory, ttl) {
    this.factory = factory;
    this.cache = new Cache(ttl, function(link) { link.close(); });
}

LinkCache.prototype.clear = function () {
    this.cache.clear();
}

LinkCache.prototype.get = function (address) {
    var link = this.cache.get(address);
    if (link === undefined) {
        link = this.factory(address);
        this.cache.put(address, link);
    }
    return link;
};

var Server = function (container, address, options) {
    this.options = options || {};
    var u = url.parse(address);
    //TODO: handle scheme and user/password if present
    this.connection = container.connect({'host':u.hostname, 'port':u.port});
    this.connection.on('connection_open', this._connection_open.bind(this));
    this.connection.on('message', this._request.bind(this));
    this.receiver = this.connection.attach_receiver(u.path.substr(1));
    this.callbacks = {};
    this._send = undefined;
    this._clear = undefined;
};

function match(desired, offered) {
    if (offered) {
        if (Array.isArray(offered)) {
            return offered.indexOf(desired) > -1;
        } else {
            return desired === offered;
        }
    } else {
        return false;
    }
}

Server.prototype._connection_open = function (context) {
    if (match('ANONYMOUS-RELAY', this.connection.remote.open.offered_capabilities)) {
        var relay = this.connection.attach_sender({target:{}});
        this._send = function (msg) { relay.send(msg); };
    } else {
        var cache = new LinkCache(this.connection.attach_sender.bind(this.connection), this.options.cache_ttl || 60000);
        this._send = function (msg) { var s = cache.get(msg.properties.to); if (s) s.send(msg); };
        this._clear = function () { cache.clear(); }
    }
}

Server.prototype._respond = function (response) {
    var server = this;
    return function (result, error) {
        if (error) {
            response.properties.subject = error.name || 'error';
            response.body = error.description || error;
        } else {
            response.properties.subject = 'ok';
            response.body = result;
        }
        server._send(response);
    };
}

Server.prototype._request = function (context) {
    var request = context.message;
    var response = {properties:{}};
    response.properties.to = request.properties.reply_to;
    response.properties.correlation_id = request.properties.message_id;
    var callback = this.callbacks[request.properties.subject];
    if (callback) {
        callback(request.body, this._respond(response));
    } else {
        response.properties.subject = 'bad-method';
        response.body = 'Unrecognised method ' + request.properties.subject;
        this._send(response);
    }
};

Server.prototype.bind_sync = function (f, name) {
    this.callbacks[name || f.name] = function (args, callback) { var result = f(args); callback(result); };
};
Server.prototype.bind = function (f, name) {
    this.callbacks[name || f.name] = f;
};

Server.prototype.close = function () {
    if (this._clear) this._clear();
    this.receiver.close();
    this.connection.close();
};

module.exports = {
    server : function(container, address, options) { return new Server(container, address, options); },
    client : function(connection, address) { return new Client(connection, address); }
};

},{"url":29}],8:[function(require,module,exports){
(function (Buffer){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var frames = require('./frames.js');
var log = require('./log.js');
var Transport = require('./transport.js');

var sasl_codes = {
    "OK":0,
    "AUTH":1,
    "SYS":2,
    "SYS_PERM":3,
    "SYS_TEMP":4,
};

var SASL_PROTOCOL_ID = 0x03;

function intersection(lista, listb) {
    return lista.filter(function (a) { return listb.indexOf(a) >= 0; });
}
function extract(buffer) {
    var results = [];
    var start = 0;
    var i = 0;
    while (i < buffer.length) {
        if (buffer[i] === 0x00) {
            if (i > start) results.push(buffer.toString('utf8', start, i));
            else results.push(null);
            start = ++i;
        } else {
            ++i;
        }
    }
    if (i > start) results.push(buffer.toString('utf8', start, i));
    else results.push(null);
    return results;
}

var PlainServer = function(callback) {
    this.callback = callback;
    this.outcome = undefined;
    this.username = undefined;
};

PlainServer.prototype.start = function(response) {
    var fields = extract(response);
    if (fields.length !== 3) {
        this.connection.sasl_failed('Unexpected response in PLAIN, got ' + fields.length + ' fields, expected 3');
    }
    if (this.callback(fields[1], fields[2])) {
        this.outcome = true;
        this.username = fields[1];
    } else {
        this.outcome = false;
    }
};

var PlainClient = function(username, password) {
    this.username = username;
    this.password = password;
};

PlainClient.prototype.start = function() {
    var response = new Buffer(1 + this.username.length + 1 + this.password.length);
    response.writeUInt8(0, 0);
    response.write(this.username, 1);
    response.writeUInt8(0, 1 + this.username.length);
    response.write(this.password, 1 + this.username.length + 1);
    return response;
};

var AnonymousServer = function() {
    this.outcome = undefined;
    this.username = undefined;
};

AnonymousServer.prototype.start = function(response) {
    this.outcome = true;
    this.username = response ? response.toString('utf8') : 'anonymous';
};

var AnonymousClient = function(name) {
    this.username = name ? name : 'anonymous';
};

AnonymousClient.prototype.start = function() {
    var response = new Buffer(1 + this.username.length);
    response.writeUInt8(0, 0);
    response.write(this.username, 1);
    return response;
};

var ExternalServer = function() {
    this.outcome = undefined;
    this.username = undefined;
};

ExternalServer.prototype.start = function(response) {
    this.outcome = true;
};

var ExternalClient = function() {
    this.username = undefined;
};

ExternalClient.prototype.start = function() {
    return null;
};

/**
 * The mechanisms argument is a map of mechanism names to factory
 * functions for objects that implement that mechanism.
 */
var SaslServer = function (connection, mechanisms) {
    this.connection = connection;
    this.transport = new Transport(connection.amqp_transport.identifier, SASL_PROTOCOL_ID, frames.TYPE_SASL, this);
    this.next = connection.amqp_transport;
    this.mechanisms = mechanisms;
    this.mechanism = undefined;
    this.outcome = undefined;
    this.username = undefined;
    var mechlist = Object.getOwnPropertyNames(mechanisms);
    this.transport.encode(frames.sasl_frame(frames.sasl_mechanisms({sasl_server_mechanisms:mechlist}).described()));
};

SaslServer.prototype.do_step = function (challenge) {
    if (this.mechanism.outcome === undefined) {
        this.transport.encode(frames.sasl_frame(frames.sasl_challenge({'challenge':challenge}).described()));
    } else {
        this.outcome = this.mechanism.outcome ? sasl_codes.OK : sasl_codes.AUTH;
        this.transport.encode(frames.sasl_frame(frames.sasl_outcome({code: this.outcome}).described()));
        if (this.outcome === sasl_codes.OK) {
            this.username = this.mechanism.username;
            this.transport.write_complete = true;
            this.transport.read_complete = true;
        }
    }
};

SaslServer.prototype.on_sasl_init = function (frame) {
    var f = this.mechanisms[frame.performative.mechanism];
    if (f) {
        this.mechanism = f();
        var challenge = this.mechanism.start(frame.performative.initial_response);
        this.do_step(challenge);
    } else {
        this.outcome = sasl_codes.AUTH;
        this.transport.encode(frames.sasl_frame(frames.sasl_outcome({code: this.outcome}).described()));
    }
};
SaslServer.prototype.on_sasl_response = function (frame) {
    this.do_step(this.mechanism.step(frame.performative.response));
};

SaslServer.prototype.has_writes_pending = function () {
    return this.transport.has_writes_pending() || this.next.has_writes_pending();
}

SaslServer.prototype.write = function (socket) {
    if (this.transport.write_complete && this.transport.pending.length === 0) {
        return this.next.write(socket);
    } else {
        return this.transport.write(socket);
    }
};

SaslServer.prototype.read = function (buffer) {
    if (this.transport.read_complete) {
        return this.next.read(buffer);
    } else {
        return this.transport.read(buffer);
    }
};

var SaslClient = function (connection, mechanisms) {
    this.connection = connection;
    this.transport = new Transport(connection.amqp_transport.identifier, SASL_PROTOCOL_ID, frames.TYPE_SASL, this);
    this.next = connection.amqp_transport;
    this.mechanisms = mechanisms;
    this.mechanism = undefined;
    this.mechanism_name = undefined;
    this.failed = false;
};

SaslClient.prototype.on_sasl_mechanisms = function (frame) {
    for (var i = 0; this.mechanism === undefined && i < frame.performative.sasl_server_mechanisms.length; i++) {
        var mech = frame.performative.sasl_server_mechanisms[i];
        var f = this.mechanisms[mech];
        if (f) {
            this.mechanism = f();
            this.mechanism_name = mech;
        }
    }
    if (this.mechanism) {
        var response = this.mechanism.start();
        this.transport.encode(frames.sasl_frame(frames.sasl_init({'mechanism':this.mechanism_name,'initial_response':response}).described()));
    } else {
        this.failed = true;
        this.connection.sasl_failed('No suitable mechanism; server supports ' + frame.performative.sasl_server_mechanisms);
    }
};
SaslClient.prototype.on_sasl_challenge = function (frame) {
    var response = this.mechanism.step(frame.performative.challenge);
    this.transport.encode(frames.sasl_frame(frames.sasl_response({'response':response}).described()));
};
SaslClient.prototype.on_sasl_outcome = function (frame) {
    switch (frame.performative.code) {
    case sasl_codes.OK:
        this.transport.read_complete = true;
        this.transport.write_complete = true;
        break;
    default:
        this.transport.write_complete = true;
        this.connection.sasl_failed("Failed to authenticate: " + frame.performative.code);
    }
};

SaslClient.prototype.has_writes_pending = function () {
    return this.transport.has_writes_pending() || this.next.has_writes_pending();
}

SaslClient.prototype.write = function (socket) {
    if (this.transport.write_complete) {
        return this.next.write(socket);
    } else {
        return this.transport.write(socket);
    }
};

SaslClient.prototype.read = function (buffer) {
    if (this.transport.read_complete) {
        return this.next.read(buffer);
    } else {
        return this.transport.read(buffer);
    }
};

var default_server_mechanisms = {
    enable_anonymous: function () {
        this['ANONYMOUS'] = function() { return new AnonymousServer(); };
    },
    enable_plain: function (callback) {
        this['PLAIN'] = function() { return new PlainServer(callback); };
    }
};

var default_client_mechanisms = {
    enable_anonymous: function (name) {
        this['ANONYMOUS'] = function() { return new AnonymousClient(name); };
    },
    enable_plain: function (username, password) {
        this['PLAIN'] = function() { return new PlainClient(username, password); };
    },
    enable_external: function () {
        this['EXTERNAL'] = function() { return new ExternalClient(); };
    }
};

module.exports = {
    Client : SaslClient,
    Server : SaslServer,
    server_mechanisms : function () {
        return Object.create(default_server_mechanisms);
    },
    client_mechanisms : function () {
        return Object.create(default_client_mechanisms);
    },
    server_add_external: function (mechs) {
        mechs['EXTERNAL'] = function() { return new ExternalServer(); };
        return mechs;
    }
};

}).call(this,require("buffer").Buffer)
},{"./frames.js":3,"./log.js":5,"./transport.js":11,"buffer":19}],9:[function(require,module,exports){
(function (Buffer){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var frames = require('./frames.js');
var link = require('./link.js');
var log = require('./log.js');
var message = require('./message.js');
var types = require('./types.js');
var util = require('./util.js');
var EndpointState = require('./endpoint.js');

var EventEmitter = require('events').EventEmitter;

var CircularBuffer = function (capacity) {
    this.capacity = capacity;
    this.size = 0;
    this.head = 0;
    this.tail = 0;
    this.entries = [];
};

CircularBuffer.prototype.available = function () {
    return this.capacity - this.size;
};

CircularBuffer.prototype.push = function (o) {
    if (this.size < this.capacity) {
        this.entries[this.tail] = o;
        this.tail = (this.tail + 1) % this.capacity;
        this.size++;
    } else {
        throw Error('circular buffer overflow: head=' + this.head + ' tail=' + this.tail + ' size=' + this.size + ' capacity=' + this.capacity);
    }
};

CircularBuffer.prototype.pop_if = function (f) {
    var count = 0;
    while (this.size && f(this.entries[this.head])) {
        this.entries[this.head] = undefined;
        this.head = (this.head + 1) % this.capacity;
        this.size--;
        count++;
    }
    return count;
};

CircularBuffer.prototype.by_id = function (id) {
    if (this.size > 0) {
        var gap = id - this.entries[this.head].id;
        if (gap < this.size) {
            return this.entries[(this.head + gap) % this.capacity];
        }
    }
    return undefined;
};

CircularBuffer.prototype.get_head = function (id) {
    return this.size > 0 ? this.entries[this.head] : undefined;
};


var Outgoing = function () {
    this.deliveries = new CircularBuffer(2048/*TODO= configurable?*/);
    this.updated = [];
    this.next_delivery_id = 0;
    this.next_pending_delivery = 0;
    this.next_transfer_id = 0;
    this.window = types.MAX_UINT;
    this.remote_next_transfer_id = undefined;
    this.remote_window = undefined;
};

Outgoing.prototype.available = function () {
    return this.deliveries.available();
};

Outgoing.prototype.send = function (sender, tag, data, format) {
    var d = {'id':this.next_delivery_id++,
             'tag':tag,
             'link':sender,
             'data': data,
             'format':format ? format : 0,
             'sent': false,
             'settled': false,
             'state': undefined,
             'remote_settled': false,
             'remote_state': undefined};
    this.deliveries.push(d);
    return d;
};

Outgoing.prototype.on_begin = function (fields) {
    this.remote_window = fields.incoming_window;
};

Outgoing.prototype.on_flow = function (fields) {
    this.remote_next_transfer_id = fields.next_incoming_id;
    this.remote_window = fields.incoming_window;
};

Outgoing.prototype.on_disposition = function (fields) {
    var last = fields.last ? fields.last : fields.first;
    for (var i = fields.first; i <= last; i++) {
        var d = this.deliveries.by_id(i);
        if (!d) {
            console.log('Could not find delivery for ' + i + ' [' + JSON.stringify(fields) + ']');
        }
        if (d && !d.remote_settled) {
            var updated = false;
            if (fields.settled) {
                d.remote_settled = fields.settled;
                updated = true;
            }
            if (fields.state && fields.state !== d.remote_state) {
                d.remote_state = fields.state;
                updated = true;
            }
            if (updated) {
                this.updated.push(d);
            }
        }
    }
};

Outgoing.prototype.transfer_window = function() {
    if (this.remote_window) {
        return this.remote_window - (this.next_transfer_id - this.remote_next_transfer_id);
    }
};

Outgoing.prototype.process = function() {
    // send pending deliveries for which there is credit:
    while (this.next_pending_delivery < this.next_delivery_id) {
        var d = this.deliveries.by_id(this.next_pending_delivery);
        if (d) {
            if (d.link.has_credit()) {
                d.link.delivery_count++;
                //TODO: fragment as appropriate
                d.transfers_required = 1;
                if (this.transfer_window() >= d.transfers_required) {
                    this.next_transfer_id += d.transfers_required;
                    this.window -= d.transfers_required;
                    d.link.session.output(frames.transfer({'handle':d.link.local.handle,'message_format':d.format,'delivery_id':d.id, 'delivery_tag':d.tag}).described(), d.data);
                    d.link.credit--;
                    this.next_pending_delivery++;
                } else {
                    log.flow('Incoming window of peer preventing sending further transfers: remote_window=' + this.remote_window + ", remote_next_transfer_id=" + this.remote_next_transfer_id
                               + ", next_transfer_id=" + this.next_transfer_id);
                    break;
                }
            } else {
                log.flow('Link has no credit');
                break;
            }
        } else {
            console.log('ERROR: Next pending delivery not found: ' + this.next_pending_delivery);
            break;
        }
    }

    // notify application of any updated deliveries:
    for (var i = 0; i < this.updated.length; i++) {
        var d = this.updated[i];
        if (d.remote_state) {
            d.remote_state = message.unwrap_outcome(d.remote_state);
            if (d.remote_state && d.remote_state.constructor.composite_type) {
                d.link.dispatch(d.remote_state.constructor.composite_type, d.link._context({'delivery':d}));
            }
        }
        if (d.remote_settled) d.link.dispatch('settled', d.link._context({'delivery':d}));
    }
    this.updated = [];

    // remove any fully settled deliveries:
    this.deliveries.pop_if(function (d) { return d.settled && d.remote_settled; });
};

var Incoming = function () {
    this.deliveries = new CircularBuffer(2048/*TODO: configurable?*/);
    this.updated = [];
    this.next_transfer_id = 0;
    this.next_delivery_id = undefined;
    this.window = 2048/*TODO: configurable?*/;
    this.remote_next_transfer_id = undefined;
    this.remote_window = undefined;
};

Incoming.prototype.update = function (delivery, settled, state) {
    if (delivery) {
        delivery.settled = settled;
        if (state !== undefined) delivery.state = state;
        if (!delivery.remote_settled) {
            this.updated.push(delivery);
        }
        delivery.link.connection._register();
    }
};

Incoming.prototype.on_transfer = function(frame, receiver) {
    this.next_transfer_id++;
    if (receiver.is_open()) {
        if (this.next_delivery_id === undefined) {
            this.next_delivery_id = frame.performative.delivery_id;
        }
        var current;
        var data;
        var last = this.deliveries.get_head();
        if (last && last.incomplete) {
            if (frame.performative.delivery_id !== undefined && this.next_delivery_id != frame.performative.delivery_id) {
                //TODO: better error handling
                throw Error("frame sequence error: delivery " + this.next_delivery_id + " not complete, got " + frame.performative.delivery_id);
            }
            current = last;
            data = Buffer.concat([current.data, frame.payload], current.data.size() + frame.payload.size());
        } else if (this.next_delivery_id === frame.performative.delivery_id) {
            current = {'id':frame.performative.delivery_id,
                       'tag':frame.performative.delivery_tag,
                       'link':receiver,
                       'settled': false,
                       'state': undefined,
                       'remote_settled': frame.performative.settled,
                       'remote_state': undefined};
            var self = this;
            current.update = function (settled, state) { self.update(current, settled, state); };
            this.deliveries.push(current);
            data = frame.payload;
        } else {
            //TODO: better error handling
            throw Error("frame sequence error: expected " + this.next_delivery_id + ", got " + frame.performative.delivery_id);
        }
        current.incomplete = frame.performative.more;
        if (current.incomplete) {
            current.data = data;
        } else {
            receiver.credit--;
            receiver.delivery_count++;
            this.next_delivery_id++;
            receiver.dispatch('message', receiver._context({'message':message.decode(data), 'delivery':current}));
        }
    }
};

Incoming.prototype.process = function () {
    if (this.updated.length > 0) {
        var first;
        var last;
        var next_id;

        for (var i = 0; i < this.updated.length; i++) {
            var delivery = this.updated[i];
            if (first === undefined) {
                first = delivery;
                last = delivery;
                next_id = delivery.id;
            }

            if (!message.are_outcomes_equivalent(last.state, delivery.state) || last.settled !== delivery.settled || next_id !== delivery.id) {
                first.link.session.output(frames.disposition({'role':true,'first':first.id,'last':last.id, 'state':first.state, 'settled':first.settled}).described());
                first = delivery;
                last = delivery;
                next_id = delivery.id;
            } else {
                if (last.id !== delivery.id) {
                    last = delivery;
                }
                next_id++;
            }
        }
        if (first !== undefined && last !== undefined) {
            first.link.session.output(frames.disposition({'role':true,'first':first.id,'last':last.id, 'state':first.state, 'settled':first.settled}).described());
        }

        this.updated = [];
    }

    // remove any fully settled deliveries:
    this.deliveries.pop_if(function (d) { return d.settled; });
};

Incoming.prototype.on_begin = function (fields) {
    this.remote_window = fields.outgoing_window;
};

Incoming.prototype.on_flow = function (fields) {
    this.remote_next_transfer_id = fields.next_outgoing_id;
    this.remote_window = fields.outgoing_window;
};

Incoming.prototype.on_disposition = function (fields) {
    var last = fields.last ? fields.last : fields.first;
    for (var i = fields.first; i <= last; i++) {
        var d = this.deliveries.by_id(i);
        if (!d) {
            console.log('Could not find delivery for ' + i);
        }
        if (d && !d.remote_settled) {
            var updated = false;
            if (fields.settled) {
                d.remote_settled = fields.settled;
                updated = true;
            }
            if (fields.state && fields.state !== d.remote_state) {
                d.remote_state = fields.state;
                updated = true;
            }
            if (updated) {
                console.log(d.link.connection.options.id + ' added delivery to updated list following receipt of disposition for incoming deliveries');
                this.updated.push(d);
            }
        }
    }

};

var Session = function (connection, local_channel) {
    this.connection = connection;
    this.outgoing = new Outgoing();
    this.incoming = new Incoming();
    this.state = new EndpointState();
    this.local = {'channel': local_channel, 'handles':{}};
    this.local.begin = frames.begin({next_outgoing_id:this.outgoing.next_transfer_id,incoming_window:this.incoming.window,outgoing_window:this.outgoing.window});
    this.local.end = frames.end();
    this.remote = {'handles':{}};
    this.links = {}; // map by name
    this.options = {};
};
Session.prototype = Object.create(EventEmitter.prototype);
Session.prototype.constructor = Session;

Session.prototype.reset = function() {
    this.state.disconnected();
    this.outgoing = new Outgoing();
    this.incoming = new Incoming();
    this.remote = {'handles':{}};
    for (var l in this.links) {
        this.links[l].reset();
    }
};

Session.prototype.dispatch = function(name, context) {
    log.events('Session got event: '+ name);
    if (this.listeners(name).length) {
        EventEmitter.prototype.emit.apply(this, arguments);
    } else {
        this.connection.dispatch.apply(this.connection, arguments);
    }
};
Session.prototype.output = function (frame, payload) {
    this.connection._write_frame(this.local.channel, frame, payload);
};

Session.prototype.create_sender = function (name, opts) {
    return this.create_link(name, link.Sender, opts);
};

Session.prototype.create_receiver = function (name, opts) {
    return this.create_link(name, link.Receiver, opts);
};

function attach(factory, args, remote_terminus) {
    var opts = args ? args : {};
    if (typeof args === 'string') {
        opts = {};
        opts[remote_terminus] = args;
    }
    if (!opts.name) opts.name = util.generate_uuid();
    var l = factory(opts.name, opts);
    for (var t in {'source':0, 'target':0}) {
        if (opts[t]) {
            if (typeof opts[t] === 'string') {
                opts[t] = {'address' : opts[t]};
            }
            l['set_' + t](opts[t]);
        }
    }
    l.attach();
    return l;
}

Session.prototype.get_option = function (name, default_value) {
    if (this.options[name] !== undefined) return this.options[name];
    else return this.connection.get_option(name, default_value);
};

Session.prototype.attach_sender = function (args) {
    return attach(this.create_sender.bind(this), args, 'target');
};
Session.prototype.open_sender = Session.prototype.attach_sender;//alias

Session.prototype.attach_receiver = function (args) {
    return attach(this.create_receiver.bind(this), args, 'source');
};
Session.prototype.open_receiver = Session.prototype.attach_receiver;//alias

Session.prototype.create_link = function (name, constructor, opts) {
    var i = 0;
    while (this.local.handles[i]) i++;
    var l = new constructor(this, name, i, opts);
    this.links[name] = l;
    this.local.handles[i] = l;
    return l;
};

Session.prototype.begin = function () {
    if (this.state.open()) {
        this.connection._register();
    }
};
Session.prototype.open = Session.prototype.begin;

Session.prototype.end = function () {
    if (this.state.close()) {
        this.connection._register();
    }
};
Session.prototype.close = Session.prototype.end;

Session.prototype.is_open = function () {
    return this.connection.is_open() && this.state.is_open();
};

Session.prototype.is_closed = function () {
    return this.connection.is_closed() || this.state.is_closed();
};

Session.prototype._process = function () {
    do {
        if (this.state.need_open()) {
            this.output(this.local.begin.described());
        }

        this.outgoing.process();
        this.incoming.process();
        for (var k in this.links) {
            this.links[k]._process();
        }

        if (this.state.need_close()) {
            this.output(this.local.end.described());
        }
    } while (!this.state.has_settled());
};

Session.prototype.send = function (sender, tag, data, format) {
    var d = this.outgoing.send(sender, tag, data, format);
    this.connection._register();
    return d;
};

Session.prototype._write_flow = function (link) {
    var fields = {'next_incoming_id':this.incoming.next_transfer_id,
                  'incoming_window':this.incoming.window,
                  'next_outgoing_id':this.outgoing.next_transfer_id,
                  'outgoing_window':this.outgoing.window
                 };
    if (link) {
        fields.delivery_count = link.delivery_count;
        fields.handle = link.local.handle;
        fields.link_credit = link.credit;
    }
    this.output(frames.flow(fields).described());
};

Session.prototype.on_begin = function (frame) {
    if (this.state.remote_opened()) {
        if (!this.remote.channel) {
            this.remote.channel = frame.channel;
        }
        this.remote.begin = frame.performative;
        this.outgoing.on_begin(frame.performative);
        this.incoming.on_begin(frame.performative);
        this.open();
        this.dispatch('session_open', this._context());
    } else {
        throw Error('Begin already received');
    }
};
Session.prototype.on_end = function (frame) {
    if (this.state.remote_closed()) {
        this.remote.end = frame.performative;
        this.close();
        this.dispatch('session_close', this._context());
    } else {
        throw Error('End already received');
    }
};

Session.prototype.on_attach = function (frame) {
    var name = frame.performative.name;
    var link = this.links[name];
    if (!link) {
        // if role is true, peer is receiver, so we are sender
        link = frame.performative.role ? this.create_sender(name) : this.create_receiver(name);
    }
    this.remote.handles[frame.performative.handle] = link;
    link.on_attach(frame);
    link.remote.attach = frame.performative;
};

Session.prototype.on_disposition = function (frame) {
    if (frame.performative.role) {
        log.events('Received disposition for outgoing transfers');
        this.outgoing.on_disposition(frame.performative);
    } else {
        log.events('Received disposition for incoming transfers');
        this.incoming.on_disposition(frame.performative);
    }
    this.connection._register();
}

Session.prototype.on_flow = function (frame) {
    this.outgoing.on_flow(frame.performative);
    this.incoming.on_flow(frame.performative);
    if (frame.performative.handle !== undefined) {
        this._get_link(frame).on_flow(frame);
    }
    this.connection._register();
}
Session.prototype._context = function (c) {
    var context = c ? c : {};
    context.session = this;
    return this.connection._context(context);
};

Session.prototype._get_link = function (frame) {
    var handle = frame.performative.handle;
    var link = this.remote.handles[handle];
    if (!link) {
        throw Error('Invalid handle ' + handle);
    }
    return link;
};

Session.prototype.on_detach = function (frame) {
    this._get_link(frame).on_detach(frame);
    //remove link
    var handle = frame.performative.handle;
    var link = this.remote.handles[handle];
    delete this.remote.handles[handle];
    delete this.local.handles[link.local.handle];
    delete this.links[link.name];
};

Session.prototype.on_transfer = function (frame) {
    this.incoming.on_transfer(frame, this._get_link(frame));
};

module.exports = Session;

}).call(this,require("buffer").Buffer)
},{"./endpoint.js":2,"./frames.js":3,"./link.js":4,"./log.js":5,"./message.js":6,"./types.js":12,"./util.js":13,"buffer":19,"events":23}],10:[function(require,module,exports){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var types = require('./types.js');

var terminus = {};
var by_descriptor = {};

function define_terminus(def) {
    var c = types.define_composite(def);
    terminus[def.name] = c.create;
    by_descriptor[Number(c.descriptor.numeric).toString(10)] = c;
    by_descriptor[c.descriptor.symbolic] = c;
};

terminus.unwrap = function(field) {
    if (field && field.descriptor) {
        var c = by_descriptor[field.descriptor.value];
        if (c) {
            return new c(field.value)
        } else {
            console.log('Unknown terminus: ' + field.descriptor);
        }
    }
    return null;
};

define_terminus(
    {name:"source",
     code:0x28,
     fields: [
         {name:"address", type:"string"},
         {name:"durable", type:"uint", default_value:0},
         {name:"expiry_policy", type:"symbol", default_value:"session-end"},
         {name:"timeout", type:"uint", default_value:0},
         {name:"dynamic", type:"boolean", default_value:false},
         {name:"dynamic_node_properties", type:"symbolic_map"},
         {name:"distribution_mode", type:"symbol"},
         {name:"filter", type:"symbolic_map"},
         {name:"default_outcome", type:"*"},
         {name:"outcomes", type:"symbol", multiple:true},
         {name:"capabilities", type:"symbol", multiple:true}
     ]
    });

define_terminus(
    {name:"target",
     code:0x29,
     fields: [
         {name:"address", type:"string"},
         {name:"durable", type:"uint", default_value:0},
         {name:"expiry_policy", type:"symbol", default_value:"session-end"},
         {name:"timeout", type:"uint", default_value:0},
         {name:"dynamic", type:"boolean", default_value:false},
         {name:"dynamic_node_properties", type:"symbolic_map"},
         {name:"capabilities", type:"symbol", multiple:true}
     ]
    });

module.exports = terminus;

},{"./types.js":12}],11:[function(require,module,exports){
(function (Buffer){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var frames = require('./frames.js');
var log = require('./log.js');


var Transport = function (identifier, protocol_id, frame_type, handler) {
    this.identifier = identifier;
    this.protocol_id = protocol_id;
    this.frame_type = frame_type;
    this.handler = handler;
    this.pending = [];
    this.header_sent = undefined;
    this.header_received = undefined;
    this.write_complete = false;
    this.read_complete = false;
}

Transport.prototype.has_writes_pending = function () {
    return this.pending.length > 0;
}

Transport.prototype.encode = function (frame) {
    var buffer = frames.write_frame(frame);
    log.frames('[' + this.identifier + '] PENDING: ' + JSON.stringify(frame));
    this.pending.push(buffer);
};

Transport.prototype.write = function (socket) {
    if (!this.header_sent) {
        var buffer = new Buffer(8);
        var header = {protocol_id:this.protocol_id, major:1, minor:0, revision:0};
        frames.write_header(buffer, header);
        socket.write(buffer);
        this.header_sent = header;
    }
    for (var i = 0; i < this.pending.length; i++) {
        socket.write(this.pending[i]);
        log.raw('[' + this.identifier + '] SENT: ' + JSON.stringify(this.pending[i]));
    }
    this.pending = [];
};

Transport.prototype.read = function (buffer) {
    var offset = 0;
    if (!this.header_received) {
        if (buffer.length < 8) {
            return offset;
        } else {
            this.header_received = frames.read_header(buffer);
            log.frames('[' + this.identifier + '] RECV: ' + JSON.stringify(this.header_received));
            if (this.header_received.protocol_id !== this.protocol_id) {
                throw Error('Invalid AMQP protocol id ' + this.header_received.protocol_id + ' expecting: ' + this.protocol_id);
            }
            offset = 8;
        }
    }
    while (offset < buffer.length && !this.read_complete) {
        var frame_size = buffer.readUInt32BE(offset);
        log.io('[' + this.identifier + '] got frame of size ' + frame_size);
        if (buffer.length < offset + frame_size) {
            log.io('[' + this.identifier + '] incomplete frame; have only ' + (buffer.length - offset) + ' of ' + frame_size);
            //don't have enough data for a full frame yet
            break;
        } else {
            var frame = frames.read_frame(buffer.slice(offset, offset + frame_size));
            log.frames('[' + this.identifier + '] RECV: ' + JSON.stringify(frame));
            if (frame.type !== this.frame_type) {
                throw Error('Invalid frame type: ' + frame.type);
            }
            offset += frame_size;
            if (frame.performative) {
                frame.performative.dispatch(this.handler, frame);
            }
        }
    }
    return offset;
}

module.exports = Transport

}).call(this,require("buffer").Buffer)
},{"./frames.js":3,"./log.js":5,"buffer":19}],12:[function(require,module,exports){
(function (Buffer){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

function Typed(type, value) {
    this.type = type;
    this.value = value;
}
Typed.prototype.toString = function() {
    return this.value ? this.value.toString() : null;
}
Typed.prototype.toLocaleString = function() {
    return this.value ? this.value.toLocaleString() : null;
}
Typed.prototype.valueOf = function() {
    return this.value;
}
Typed.prototype.toJSON = function() {
    return this.value && this.value.toJSON ? this.value.toJSON() : this.value;
}

function hex(i) {
    return Number(i).toString(16);
}

var types = {'by_code':{}};
Object.defineProperty(types, 'MAX_UINT', {value: 4294967295, writable: false, configurable: false});
Object.defineProperty(types, 'MAX_USHORT', {value: 65535, writable: false, configurable: false});

function define_type(name, typecode, annotations, empty_value) {
    var subcategory = typecode >>> 4;
    var t;
    if (subcategory === 0x4) {
        // constructors for 'empty' types don't take a value
        t = function () {
            this.type = t;
            this.value = empty_value;
        };
    } else if (subcategory === 0xE || subcategory === 0xF) {
        t = function (v, code, descriptor) {
            this.type = t;
            this.value = v;
            this.array_constructor = {'typecode':code};
            if (descriptor) {
                this.array_constructor.descriptor = descriptor;
            }
        };
    } else {
        t = function (v) {
            this.type = t;
            this.value = v;
        };
    }
    t.typecode = typecode;
    t.prototype = Object.create(Typed.prototype);
    t.toString = function () {
        return name + '#' + hex(typecode);
    };
    switch (subcategory) {
    case 0x4:
        t.width = 0;
        t.category = 'fixed';
        break;
    case 0x5:
        t.width = 1;
        t.category = 'fixed';
        break;
    case 0x6:
        t.width = 2;
        t.category = 'fixed';
        break;
    case 0x7:
        t.width = 4;
        t.category = 'fixed';
        break;
    case 0x8:
        t.width = 8;
        t.category = 'fixed';
        break;
    case 0x9:
        t.width = 16;
        t.category = 'fixed';
        break;
    case 0xA:
        t.width = 1;
        t.category = 'variable';
        break;
    case 0xB:
        t.width = 4;
        t.category = 'variable';
        break;
    case 0xC:
        t.width = 1;
        t.category = 'compound';
        break;
    case 0xD:
        t.width = 4;
        t.category = 'compound';
        break;
    case 0xE:
        t.width = 1;
        t.category = 'array';
        break;
    case 0xF:
        t.width = 4;
        t.category = 'array';
        break;
    }
    if (t.category === 'fixed') {
        t.prototype.encoded_size = function () {
            return this.type.width;
        }
    } else if (t.category === 'variable') {
        t.prototype.encoded_size = function () {
            return this.type.width + this.value.length;
        }
    } else if (t.category === 'compound') {
        t.prototype.encoded_size = function () {
            var s = this.type.width*2;
            for (i in this.value) {
                s += 1/*typecode*/ + i.encoded_size();//what if i is described????
            }
            return s;
        }
    }
    if (annotations) {
        for (var a in annotations) {
            t[a] = annotations[a];
        }
    }
    types.by_code[t.typecode] = t;
    types[name] = t;
    return t;
}

function buffer_ops(name) {
    return {
        'read': function (buffer, offset) { return buffer['read' + name](offset); },
        'write': function (buffer, value, offset) { buffer['write' + name](value, offset); }
    };
}

var MAX_UINT = 4294967296; // 2^32
function write_ulong(buffer, value, offset) {
    var hi = value / MAX_UINT;
    var lo = value % MAX_UINT;
    try {
        buffer.writeUInt32BE(hi, offset);
    } catch (e) {
        throw Error('Could not write high byte of ' + value + '(' + hi + ') as uint32: ' + e);
    }
    try {
        buffer.writeUInt32BE(lo, offset + 4);
    } catch (e) {
        throw Error('Could not write low byte of ' + value + '(' + lo + ') as uint32: ' + e);
    }
}

define_type('Null', 0x40, undefined, null);
define_type('Boolean', 0x56, buffer_ops('UInt8'));
define_type('True', 0x41, undefined, true);
define_type('False', 0x42, undefined, false);
define_type('Ubyte', 0x50, buffer_ops('UInt8'));
define_type('Ushort', 0x60, buffer_ops('UInt16BE'));
define_type('Uint', 0x70, buffer_ops('UInt32BE'));
define_type('SmallUint', 0x52, buffer_ops('UInt8'));
define_type('Uint0', 0x43, undefined, 0);
define_type('Ulong', 0x80, {'write':write_ulong});//TODO: how to represent 64 bit numbers?
define_type('SmallUlong', 0x53, buffer_ops('UInt8'));
define_type('Ulong0', 0x44, undefined, 0);
define_type('Byte', 0x51, buffer_ops('Int8'));
define_type('Short', 0x61, buffer_ops('Int16BE'));
define_type('Int', 0x71, buffer_ops('Int32BE'));
define_type('SmallInt', 0x54, buffer_ops('Int8'));
define_type('Long', 0x81);//TODO: how to represent 64 bit numbers?
define_type('SmallLong', 0x55, buffer_ops('Int8'));
define_type('Float', 0x72, buffer_ops('Float'));
define_type('Double', 0x82, buffer_ops('Double'));
define_type('Decimal32', 0x74);
define_type('Decimal64', 0x84);
define_type('Decimal128', 0x94);
define_type('CharUTF32', 0x73);
define_type('Timestamp', 0x83);//TODO: convert to/from Date
define_type('Uuid', 0x98);//TODO: convert to/from stringified form
define_type('Vbin8', 0xa0);
define_type('Vbin32', 0xb0);
define_type('Str8', 0xa1, {'encoding':'utf8'});
define_type('Str32', 0xb1, {'encoding':'utf8'});
define_type('Sym8', 0xa3, {'encoding':'ascii'});
define_type('Sym32', 0xb3, {'encoding':'ascii'});
define_type('List0', 0x45, undefined, []);
define_type('List8', 0xc0);
define_type('List32', 0xd0);
define_type('Map8', 0xc1);
define_type('Map32', 0xd1);
define_type('Array8', 0xe0);
define_type('Array32', 0xf0);

function is_one_of(o, typelist) {
    for (var i = 0 ; i < typelist.length; i++) {
        if (o.type.typecode === typelist[i].typecode) return true;
    }
    return false;
};
types.is_ulong = function(o) {
    return is_one_of(o, [types.Ulong, types.Ulong0, types.SmallUlong]);
};
types.is_string = function(o) {
    return is_one_of(o, [types.Str8, types.Str32]);
};
types.is_symbol = function(o) {
    return is_one_of(o, [types.Sym8, types.Sym32]);
};
types.is_list = function(o) {
    return is_one_of(o, [types.List0, types.List8, types.List32]);
};
types.is_map = function(o) {
    return is_one_of(o, [types.Map8, types.Map32]);
};

types.wrap_boolean = function(v) {
    return new types.Boolean(v);
};
types.wrap_ulong = function(l) {
    if (l === 0) return new types.Ulong0();
    else return l > 255 ? new types.Ulong(l) : new types.SmallUlong(l);
};
types.wrap_uint = function(l) {
    if (l === 0) return new types.Uint0();
    else return l > 255 ? new types.Uint(l) : new types.SmallUint(l);
};
types.wrap_ushort = function(l) {
    return new types.Ushort(l);
};
types.wrap_ubyte = function(l) {
    return new types.Ubyte(l);
};
types.wrap_long = function(l) {
    return l > 255 ? new types.Long(l) : new types.SmallLong(l);
};
types.wrap_int = function(l) {
    return l > 255 ? new types.Int(l) : new types.SmallInt(l);
};
types.wrap_short = function(l) {
    return new types.Short(l);
};
types.wrap_byte = function(l) {
    return new types.Byte(l);
};
types.wrap_float = function(l) {
    return new types.Float(l);
};
types.wrap_double = function(l) {
    return new types.Double(l);
};
types.wrap_timestamp = function(l) {
    return new types.Timestamp(l);
};
types.wrap_binary = function (s) {
    return s.length > 255 ? new types.Vbin32(s) : new types.Vbin8(s);
};
types.wrap_string = function (s) {
    return s.length > 255 ? new types.Str32(s) : new types.Str8(s);
};
types.wrap_symbol = function (s) {
    return s.length > 255 ? new types.Sym32(s) : new types.Sym8(s);
};
types.wrap_list = function(l) {
    if (l.length === 0) return new types.List0();
    var items = l.map(types.wrap);
    return new types.List32(items);
};
types.wrap_map = function(m, key_wrapper) {
    var items = [];
    for (var k in m) {
        items.push(key_wrapper ? key_wrapper(k) : types.wrap(k));
        items.push(types.wrap(m[k]));
    }
    return new types.Map32(items);
};
types.wrap_symbolic_map = function(m) {
    return types.wrap_map(m, types.wrap_symbol);
};
types.wrap_array = function(l, code, descriptors) {
    if (code) {
        return new types.Array32(l, code, descriptors);
    } else {
        throw Error('An array must specify a type for its elements');
    }
};
types.wrap = function(o) {
    var t = typeof o;
    if (t === "string") {
        return types.wrap_string(o);
    } else if (t == "boolean") {
        return o ? new types.True() : new types.False();
    } else if (t == "number" || o instanceof Number) {
        if (isNaN(o)) {
            throw Error('Cannot wrap NaN! ' + o);
        } else if (Math.floor(o) - o !== 0) {
            return new types.Double(o);
        } else if (o > 0) {
            return types.wrap_uint(o);
        } else {
            return types.wrap_int(o);
        }
    } else if (o instanceof Date) {
        //??? TODO ???
    } else if (o instanceof Typed) {
        return o;
    } else if (t == "undefined" || o === null) {
        return new types.Null();
    } else if (Array.isArray(o)) {
        return types.wrap_list(o);
    } else {
        return types.wrap_map(o);
    }
};
types.wrap_described = function(value, descriptor) {
    var result = types.wrap(value);
    if (descriptor) {
        if (typeof descriptor === 'string') {
            result = types.described(types.wrap_string(descriptor), result);
        } else if (typeof descriptor === 'number' || descriptor instanceof Number) {
            result = types.described(types.wrap_ulong(descriptor), result);
        }
    }
    return result;
}

types.wrap_message_id = function(o) {
    var t = typeof o
    if (t === "string") {
        return types.wrap_string(o);
    } else if (t == "number" || o instanceof Number) {
        return types.wrap_ulong(o);
    } else {
        //TODO handle uuids
        throw Error('invalid message id:' + o);
    }
};
types.wrap_delivery_state = function(o) {
    //TODO
    return new Null;
};

/**
 * Converts the list of keys and values that comprise an AMQP encoded
 * map into a proper javascript map/object.
 */
function mapify(elements) {
    var result = {};
    for (var i = 0; i+1 < elements.length;) {
        result[elements[i++]] = elements[i++];
    }
    return result;
}

var by_descriptor = {};

function unwrap_described(o) {
    if (o.descriptor) {
        var c = by_descriptor[o.descriptor.value];
        if (c) {
            return new c(o.value)
        }
    }
    return undefined;
};

types.unwrap = function(o, leave_described) {
    if (o instanceof Typed) {
        if (o.descriptor) {
            var c = by_descriptor[o.descriptor.value];
            if (c) {
                return new c(o.value)
            } else if (leave_described) {
                return o;
            }
        }
        var u = types.unwrap(o.value, true);
        return types.is_map(o) ? mapify(u) : u;
    } else if (Array.isArray(o)) {
        return o.map(function (i) { return types.unwrap(i, true); });
    } else {
        return o;
    }
};

types.described = function (descriptor, typedvalue) {
    var o = Object.create(typedvalue);
    if (descriptor.length) {
        o.descriptor = descriptor.shift();
        return described(descriptor, o);
    } else {
        o.descriptor = descriptor;
        return o;
    }
}

function get_type(code) {
    var type = types.by_code[code];
    if (!type) {
        throw Error('Unrecognised typecode: ' + hex(code));
    }
    return type;
}

types.Reader = function (buffer) {
    this.buffer = buffer;
    this.position = 0;
}

types.Reader.prototype.read_typecode = function () {
    return this.read_uint(1);
}

types.Reader.prototype.read_uint = function (width) {
    var current = this.position;
    this.position += width;
    var name = width > 1 ? 'readUInt' + (width * 8) + 'BE' : 'readUInt' + 8;
    return this.buffer[name](current);
}

types.Reader.prototype.read_fixed_width = function (type) {
    var current = this.position;
    this.position += type.width;
    if (type.read) {
        return type.read(this.buffer, current);
    } else {
        return this.buffer.slice(current, this.position);
    }
}

types.Reader.prototype.read_variable_width = function (type) {
    var size = this.read_uint(type.width);
    var slice = this.read_bytes(size);
    return type.encoding ? slice.toString(type.encoding) : slice;
}

types.Reader.prototype.read = function () {
    var constructor = this.read_constructor();
    var value = this.read_value(get_type(constructor.typecode));
    return constructor.descriptor ? types.described(constructor.descriptor, value) : value;
}

types.Reader.prototype.read_constructor = function () {
    var code = this.read_typecode();
    if (code === 0x00) {
        var d = [];
        d.push(this.read());
        var c = this.read_constructor();
        while (c.descriptor) {
            d.push(c.descriptor);
            c = this.read_constructor();
        }
        return {'typecode': c.typecode, 'descriptor':  d.length == 1 ? d[0] : d};
    } else {
        return {'typecode': code};
    }
}

types.Reader.prototype.read_value = function (type) {
    if (type.width === 0) {
        return new type();
    //TODO: use enumeration rather than string for category
    } else if (type.category === 'fixed') {
        return new type(this.read_fixed_width(type));
    } else if (type.category === 'variable') {
        return new type(this.read_variable_width(type));
    } else if (type.category === 'compound') {
        return this.read_compound(type);
    } else if (type.category === 'array') {
        return this.read_array(type);
    } else {
        throw Error('Invalid category for type: ' + type);
    }
}

types.Reader.prototype.read_multiple = function (n, f) {
    var read_fn = f ? f : this.read.bind(this);
    var items = [];
    while (items.length < n) {
        items.push(read_fn.apply(this));
    }
    return items;
}

types.Reader.prototype.read_size_count = function (width) {
    return {'size': this.read_uint(width), 'count': this.read_uint(width)};
}

types.Reader.prototype.read_compound = function (type) {
    var limits = this.read_size_count(type.width);
    return new type(this.read_multiple(limits.count));
}

types.Reader.prototype.read_array = function (type) {
    var limits = this.read_size_count(type.width);
    var constructor = this.read_constructor();
    var value = new type(this.read_multiple(limits.count, this.read_value.bind(this, get_type(constructor.typecode))), constructor.typecode, constructor.descriptor);
    return value;
}

types.Reader.prototype.toString = function () {
    var s = 'buffer@' + this.position;
    if (this.position) s += ': ';
    for (var i = this.position; i < this.buffer.length; i++) {
        if (i > 0) s+= ',';
        s += '0x' + Number(this.buffer[i]).toString(16);
    }
    return s;
}

types.Reader.prototype.reset = function () {
    this.position = 0;
}

types.Reader.prototype.skip = function (bytes) {
    this.position += bytes;
}

types.Reader.prototype.read_bytes = function (bytes) {
    var current = this.position;
    this.position += bytes;
    return this.buffer.slice(current, this.position);
}

types.Reader.prototype.remaining = function () {
    return this.buffer.length - this.position;
}

types.Writer = function (buffer) {
    this.buffer = buffer ? buffer : new Buffer(1024);
    this.position = 0;
}

types.Writer.prototype.toBuffer = function () {
    return this.buffer.slice(0, this.position);
};

function max(a, b) {
    return a > b ? a : b;
}

types.Writer.prototype.ensure = function (length) {
    if (this.buffer.length < length) {
        var bigger = new Buffer(max(this.buffer.length*2, length));
        this.buffer.copy(bigger);
        this.buffer = bigger;
    }
}

types.Writer.prototype.write_typecode = function (code) {
    this.write_uint(code, 1);
}

types.Writer.prototype.write_uint = function (value, width) {
    var current = this.position;
    this.ensure(this.position + width);
    this.position += width;
    var name = width > 1 ? 'writeUInt' + (width * 8) + 'BE' : 'writeUInt' + 8;
    if (!this.buffer[name]) {
        throw Error("Buffer doesn't define " + name);
    }
    return this.buffer[name](value, current);
}


types.Writer.prototype.write_fixed_width = function (type, value) {
    var current = this.position;
    this.ensure(this.position + type.width);
    this.position += type.width;
    if (type.write) {
        type.write(this.buffer, value, current);
    } else if (value.copy) {
        value.copy(this.buffer, current);
    } else {
        throw Error("Can't handle write for " + type);
    }
}

types.Writer.prototype.write_variable_width = function (type, value) {
    var source = type.encoding ? new Buffer(value, type.encoding) : new Buffer(value);//TODO: avoid creating new buffers
    this.write_uint(source.length, type.width);
    this.write_bytes(source);
}

types.Writer.prototype.write_bytes = function (source) {
    var current = this.position;
    this.ensure(this.position + source.length);
    this.position += source.length;
    source.copy(this.buffer, current);
}

types.Writer.prototype.write_constructor = function (typecode, descriptor) {
    if (descriptor) {
        this.write_typecode(0x00);
        this.write(descriptor);
    }
    this.write_typecode(typecode);
}

types.Writer.prototype.write = function (o) {
    if (!(o instanceof Typed)) {
        throw Error("Can't write " + JSON.stringify(o));
    }
    this.write_constructor(o.type.typecode, o.descriptor);
    this.write_value(o.type, o.value, o.array_constructor);
}

types.Writer.prototype.write_value = function (type, value, constructor/*for arrays only*/) {
    if (type.width === 0) {
        return;//nothing further to do
    //TODO: use enumeration rather than string for category
    } else if (type.category === 'fixed') {
        this.write_fixed_width(type, value);
    } else if (type.category === 'variable') {
        this.write_variable_width(type, value);
    } else if (type.category === 'compound') {
        this.write_compound(type, value);
    } else if (type.category === 'array') {
        this.write_array(type, value, constructor);
    } else {
        throw Error('Invalid category ' + type.category + ' for type: ' + type);
    }
}

types.Writer.prototype.backfill_size = function (width, saved) {
    var gap = this.position - saved;
    this.position = saved;
    this.write_uint(gap - width, width);
    this.position += (gap - width);
}

types.Writer.prototype.write_compound = function (type, value) {
    var saved = this.position;
    this.position += type.width;//skip size field
    this.write_uint(value.length, type.width);//count field
    for (var i = 0; i < value.length; i++) {
        if (value[i] === undefined || value[i] === null) {
            this.write(new types.Null);
        } else {
            this.write(value[i]);
        }
    }
    this.backfill_size(type.width, saved);
}

types.Writer.prototype.write_array = function (type, value, constructor) {
    var saved = this.position;
    this.position += type.width;//skip size field
    this.write_uint(value.length, type.width);//count field
    this.write_constructor(constructor.typecode, constructor.descriptor);
    var type = get_type(constructor.typecode);
    for (var i = 0; i < value.length; i++) {
        this.write_value(type, value[i]);
    }
    this.backfill_size(type.width, saved);
}

types.Writer.prototype.toString = function () {
    var s = 'buffer@' + this.position;
    if (this.position) s += ': ';
    for (var i = 0; i < this.position; i++) {
        if (i > 0) s+= ',';
        s += ('00' + Number(this.buffer[i]).toString(16)).slice(-2);
    }
    return s;
}

types.Writer.prototype.skip = function (bytes) {
    this.ensure(this.position + bytes);
    this.position += bytes;
}

types.Writer.prototype.clear = function () {
    this.buffer.fill(0x00);
    this.position = 0;
}

types.Writer.prototype.remaining = function () {
    return this.buffer.length - this.position;
}


function get_constructor(typename) {
    if (typename === 'symbol') {
        return {typecode:types.Sym8.typecode};
    }
    throw Error('TODO: Array of type ' + typename + ' not yet supported');
};

function wrap_field(definition, instance) {
    if (instance !== undefined && instance !== null) {
        if (Array.isArray(instance)) {
            if (!definition.multiple) {
                throw Error('Field ' + definition.name + ' does not support multiple values, got ' + JSON.stringify(instance));
            }
            var constructor = get_constructor(definition.type);
            return types.wrap_array(instance, constructor.typecode, constructor.descriptor);
        } else if (definition.type === '*') {
            return instance;
        } else {
            var wrapper = types['wrap_' + definition.type];
            if (wrapper) {
                return wrapper(instance);
            } else {
                throw Error('No wrapper for field ' + definition.name + ' of type ' + definition.type);
            }
        }
    } else if (definition.mandatory) {
        throw Error('Field ' + definition.name + ' is mandatory');
    } else {
        return new types.Null();
    }
};

function get_accessors(index, field_definition) {
    var getter = function() { return field_definition.type === '*' ? this.value[index] : types.unwrap(this.value[index])};
    var setter = function(o) { this.value[index] = wrap_field(field_definition, o); }
    return {'get': getter, 'set': setter, 'enumerable':true, 'configurable':false};
}

types.define_composite = function(def) {
    var c = function(fields) {
        this.value = fields ? fields : [];
    }
    c.descriptor = {
        numeric: def.code,
        symbolic: 'amqp:' + def.name + ':list'
    };
    c.prototype.dispatch = function (target, frame) {
        target['on_' + def.name](frame);
    };
    //c.prototype.descriptor = c.descriptor.numeric;
    //c.prototype = Object.create(types.List8.prototype);
    for (var i = 0; i < def.fields.length; i++) {
        var f = def.fields[i];
        Object.defineProperty(c.prototype, f.name, get_accessors(i, f));
    }
    c.toString = function() {
        return def.name + '#' + Number(def.code).toString(16);
    };
    c.prototype.toJSON = function() {
        var o = {};
        for (var f in this) {
            if (f !== 'value' && this[f]) {
                o[f] = this[f];
            }
        }
        return o;
    };
    c.create = function(fields) {
        var o = new c;
        for (var f in fields) {
            o[f] = fields[f];
        }
        return o;
    }
    c.prototype.described = function(fields) {
        return types.described(types.wrap_ulong(c.descriptor.numeric), types.wrap_list(this.value));
    };
    return c;
}

function add_type(def) {
    var c = types.define_composite(def);
    types['wrap_' + def.name] = function (fields) {
        return c.create(fields).described();
    };
    by_descriptor[Number(c.descriptor.numeric).toString(10)] = c;
    by_descriptor[c.descriptor.symbolic] = c;
};

add_type({name: "error",
             code: 0x1d,
             fields: [
                 {name:"condition", type:"symbol", mandatory:true},
                 {name:"description", type:"string"},
                 {name:"info", type:"map"}
             ]
         });

module.exports = types;

}).call(this,require("buffer").Buffer)
},{"buffer":19}],13:[function(require,module,exports){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var util = {};

util.generate_uuid = function () {
    // from http://stackoverflow.com/questions/105034/create-guid-uuid-in-javascript:
    var uuid = 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c) {
        var r = Math.random()*16|0, v = c == 'x' ? r : (r&0x3|0x8);
        return v.toString(16);
    });
    return uuid;
};

util.clone = function (o) {
    var copy = Object.create(o.prototype || {});
    var names = Object.getOwnPropertyNames(o);
    for (var i = 0; i < names.length; i++) {
        var key = names[i];
        copy[key] = o[key];
    }
    return copy;
}

module.exports = util;

},{}],14:[function(require,module,exports){
(function (Buffer){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

function nulltransform(data) { return data; }

function from_arraybuffer(data) {
    if (data instanceof ArrayBuffer) return new Buffer(new Uint8Array(data))
    else return new Buffer(data);
}

function to_typedarray(data) {
    return new Uint8Array(data);
}

function wrap(ws) {
    var data_recv = nulltransform;
    var data_send = nulltransform;
    if (ws.binaryType) {
        ws.binaryType = 'arraybuffer';
        data_recv = from_arraybuffer;
        data_send = to_typedarray;
    }
    return {
        end: function() {
            ws.close();
        },
        write: function(data) {
            ws.send(data_send(data), {binary:true});
        },
        on: function(event, handler) {
            if (event === 'data') {
                ws.onmessage = function(msg_evt) {
                    handler(data_recv(msg_evt.data));
                };
            } else if (event === 'end') {
                ws.onclose = handler;
            } else if (event === 'error') {
                ws.onerror = handler;
            } else {
                console.log('ERROR: Attempt to set unrecognised handler on websocket wrapper: ' + event);
            }
        },
        get_id_string: function() {
            return ws.url;
        }
    };
}

module.exports = {

    'connect': function(Impl) {
        return function (url, options) {
            return function () {
                return {
                    connect: function(port_ignore, host_ignore, options_ignore, callback) {
                        var c = new Impl(url, options);
                        c.onopen = callback;
                        return wrap(c);
                    }
                };
            };
        };
    },
    'wrap': wrap
};

}).call(this,require("buffer").Buffer)
},{"buffer":19}],15:[function(require,module,exports){

/**
 * This is the web browser implementation of `debug()`.
 *
 * Expose `debug()` as the module.
 */

exports = module.exports = require('./debug');
exports.log = log;
exports.formatArgs = formatArgs;
exports.save = save;
exports.load = load;
exports.useColors = useColors;
exports.storage = 'undefined' != typeof chrome
               && 'undefined' != typeof chrome.storage
                  ? chrome.storage.local
                  : localstorage();

/**
 * Colors.
 */

exports.colors = [
  'lightseagreen',
  'forestgreen',
  'goldenrod',
  'dodgerblue',
  'darkorchid',
  'crimson'
];

/**
 * Currently only WebKit-based Web Inspectors, Firefox >= v31,
 * and the Firebug extension (any Firefox version) are known
 * to support "%c" CSS customizations.
 *
 * TODO: add a `localStorage` variable to explicitly enable/disable colors
 */

function useColors() {
  // is webkit? http://stackoverflow.com/a/16459606/376773
  return ('WebkitAppearance' in document.documentElement.style) ||
    // is firebug? http://stackoverflow.com/a/398120/376773
    (window.console && (console.firebug || (console.exception && console.table))) ||
    // is firefox >= v31?
    // https://developer.mozilla.org/en-US/docs/Tools/Web_Console#Styling_messages
    (navigator.userAgent.toLowerCase().match(/firefox\/(\d+)/) && parseInt(RegExp.$1, 10) >= 31);
}

/**
 * Map %j to `JSON.stringify()`, since no Web Inspectors do that by default.
 */

exports.formatters.j = function(v) {
  return JSON.stringify(v);
};


/**
 * Colorize log arguments if enabled.
 *
 * @api public
 */

function formatArgs() {
  var args = arguments;
  var useColors = this.useColors;

  args[0] = (useColors ? '%c' : '')
    + this.namespace
    + (useColors ? ' %c' : ' ')
    + args[0]
    + (useColors ? '%c ' : ' ')
    + '+' + exports.humanize(this.diff);

  if (!useColors) return args;

  var c = 'color: ' + this.color;
  args = [args[0], c, 'color: inherit'].concat(Array.prototype.slice.call(args, 1));

  // the final "%c" is somewhat tricky, because there could be other
  // arguments passed either before or after the %c, so we need to
  // figure out the correct index to insert the CSS into
  var index = 0;
  var lastC = 0;
  args[0].replace(/%[a-z%]/g, function(match) {
    if ('%%' === match) return;
    index++;
    if ('%c' === match) {
      // we only are interested in the *last* %c
      // (the user may have provided their own)
      lastC = index;
    }
  });

  args.splice(lastC, 0, c);
  return args;
}

/**
 * Invokes `console.log()` when available.
 * No-op when `console.log` is not a "function".
 *
 * @api public
 */

function log() {
  // this hackery is required for IE8/9, where
  // the `console.log` function doesn't have 'apply'
  return 'object' === typeof console
    && console.log
    && Function.prototype.apply.call(console.log, console, arguments);
}

/**
 * Save `namespaces`.
 *
 * @param {String} namespaces
 * @api private
 */

function save(namespaces) {
  try {
    if (null == namespaces) {
      exports.storage.removeItem('debug');
    } else {
      exports.storage.debug = namespaces;
    }
  } catch(e) {}
}

/**
 * Load `namespaces`.
 *
 * @return {String} returns the previously persisted debug modes
 * @api private
 */

function load() {
  var r;
  try {
    r = exports.storage.debug;
  } catch(e) {}
  return r;
}

/**
 * Enable namespaces listed in `localStorage.debug` initially.
 */

exports.enable(load());

/**
 * Localstorage attempts to return the localstorage.
 *
 * This is necessary because safari throws
 * when a user disables cookies/localstorage
 * and you attempt to access it.
 *
 * @return {LocalStorage}
 * @api private
 */

function localstorage(){
  try {
    return window.localStorage;
  } catch (e) {}
}

},{"./debug":16}],16:[function(require,module,exports){

/**
 * This is the common logic for both the Node.js and web browser
 * implementations of `debug()`.
 *
 * Expose `debug()` as the module.
 */

exports = module.exports = debug;
exports.coerce = coerce;
exports.disable = disable;
exports.enable = enable;
exports.enabled = enabled;
exports.humanize = require('ms');

/**
 * The currently active debug mode names, and names to skip.
 */

exports.names = [];
exports.skips = [];

/**
 * Map of special "%n" handling functions, for the debug "format" argument.
 *
 * Valid key names are a single, lowercased letter, i.e. "n".
 */

exports.formatters = {};

/**
 * Previously assigned color.
 */

var prevColor = 0;

/**
 * Previous log timestamp.
 */

var prevTime;

/**
 * Select a color.
 *
 * @return {Number}
 * @api private
 */

function selectColor() {
  return exports.colors[prevColor++ % exports.colors.length];
}

/**
 * Create a debugger with the given `namespace`.
 *
 * @param {String} namespace
 * @return {Function}
 * @api public
 */

function debug(namespace) {

  // define the `disabled` version
  function disabled() {
  }
  disabled.enabled = false;

  // define the `enabled` version
  function enabled() {

    var self = enabled;

    // set `diff` timestamp
    var curr = +new Date();
    var ms = curr - (prevTime || curr);
    self.diff = ms;
    self.prev = prevTime;
    self.curr = curr;
    prevTime = curr;

    // add the `color` if not set
    if (null == self.useColors) self.useColors = exports.useColors();
    if (null == self.color && self.useColors) self.color = selectColor();

    var args = Array.prototype.slice.call(arguments);

    args[0] = exports.coerce(args[0]);

    if ('string' !== typeof args[0]) {
      // anything else let's inspect with %o
      args = ['%o'].concat(args);
    }

    // apply any `formatters` transformations
    var index = 0;
    args[0] = args[0].replace(/%([a-z%])/g, function(match, format) {
      // if we encounter an escaped % then don't increase the array index
      if (match === '%%') return match;
      index++;
      var formatter = exports.formatters[format];
      if ('function' === typeof formatter) {
        var val = args[index];
        match = formatter.call(self, val);

        // now we need to remove `args[index]` since it's inlined in the `format`
        args.splice(index, 1);
        index--;
      }
      return match;
    });

    if ('function' === typeof exports.formatArgs) {
      args = exports.formatArgs.apply(self, args);
    }
    var logFn = enabled.log || exports.log || console.log.bind(console);
    logFn.apply(self, args);
  }
  enabled.enabled = true;

  var fn = exports.enabled(namespace) ? enabled : disabled;

  fn.namespace = namespace;

  return fn;
}

/**
 * Enables a debug mode by namespaces. This can include modes
 * separated by a colon and wildcards.
 *
 * @param {String} namespaces
 * @api public
 */

function enable(namespaces) {
  exports.save(namespaces);

  var split = (namespaces || '').split(/[\s,]+/);
  var len = split.length;

  for (var i = 0; i < len; i++) {
    if (!split[i]) continue; // ignore empty strings
    namespaces = split[i].replace(/\*/g, '.*?');
    if (namespaces[0] === '-') {
      exports.skips.push(new RegExp('^' + namespaces.substr(1) + '$'));
    } else {
      exports.names.push(new RegExp('^' + namespaces + '$'));
    }
  }
}

/**
 * Disable debug output.
 *
 * @api public
 */

function disable() {
  exports.enable('');
}

/**
 * Returns true if the given mode name is enabled, false otherwise.
 *
 * @param {String} name
 * @return {Boolean}
 * @api public
 */

function enabled(name) {
  var i, len;
  for (i = 0, len = exports.skips.length; i < len; i++) {
    if (exports.skips[i].test(name)) {
      return false;
    }
  }
  for (i = 0, len = exports.names.length; i < len; i++) {
    if (exports.names[i].test(name)) {
      return true;
    }
  }
  return false;
}

/**
 * Coerce `val`.
 *
 * @param {Mixed} val
 * @return {Mixed}
 * @api private
 */

function coerce(val) {
  if (val instanceof Error) return val.stack || val.message;
  return val;
}

},{"ms":17}],17:[function(require,module,exports){
/**
 * Helpers.
 */

var s = 1000;
var m = s * 60;
var h = m * 60;
var d = h * 24;
var y = d * 365.25;

/**
 * Parse or format the given `val`.
 *
 * Options:
 *
 *  - `long` verbose formatting [false]
 *
 * @param {String|Number} val
 * @param {Object} options
 * @return {String|Number}
 * @api public
 */

module.exports = function(val, options){
  options = options || {};
  if ('string' == typeof val) return parse(val);
  return options.long
    ? long(val)
    : short(val);
};

/**
 * Parse the given `str` and return milliseconds.
 *
 * @param {String} str
 * @return {Number}
 * @api private
 */

function parse(str) {
  str = '' + str;
  if (str.length > 10000) return;
  var match = /^((?:\d+)?\.?\d+) *(milliseconds?|msecs?|ms|seconds?|secs?|s|minutes?|mins?|m|hours?|hrs?|h|days?|d|years?|yrs?|y)?$/i.exec(str);
  if (!match) return;
  var n = parseFloat(match[1]);
  var type = (match[2] || 'ms').toLowerCase();
  switch (type) {
    case 'years':
    case 'year':
    case 'yrs':
    case 'yr':
    case 'y':
      return n * y;
    case 'days':
    case 'day':
    case 'd':
      return n * d;
    case 'hours':
    case 'hour':
    case 'hrs':
    case 'hr':
    case 'h':
      return n * h;
    case 'minutes':
    case 'minute':
    case 'mins':
    case 'min':
    case 'm':
      return n * m;
    case 'seconds':
    case 'second':
    case 'secs':
    case 'sec':
    case 's':
      return n * s;
    case 'milliseconds':
    case 'millisecond':
    case 'msecs':
    case 'msec':
    case 'ms':
      return n;
  }
}

/**
 * Short format for `ms`.
 *
 * @param {Number} ms
 * @return {String}
 * @api private
 */

function short(ms) {
  if (ms >= d) return Math.round(ms / d) + 'd';
  if (ms >= h) return Math.round(ms / h) + 'h';
  if (ms >= m) return Math.round(ms / m) + 'm';
  if (ms >= s) return Math.round(ms / s) + 's';
  return ms + 'ms';
}

/**
 * Long format for `ms`.
 *
 * @param {Number} ms
 * @return {String}
 * @api private
 */

function long(ms) {
  return plural(ms, d, 'day')
    || plural(ms, h, 'hour')
    || plural(ms, m, 'minute')
    || plural(ms, s, 'second')
    || ms + ' ms';
}

/**
 * Pluralization helper.
 */

function plural(ms, n, name) {
  if (ms < n) return;
  if (ms < n * 1.5) return Math.floor(ms / n) + ' ' + name;
  return Math.ceil(ms / n) + ' ' + name + 's';
}

},{}],18:[function(require,module,exports){

},{}],19:[function(require,module,exports){
(function (global){
/*!
 * The buffer module from node.js, for the browser.
 *
 * @author   Feross Aboukhadijeh <feross@feross.org> <http://feross.org>
 * @license  MIT
 */
/* eslint-disable no-proto */

'use strict'

var base64 = require('base64-js')
var ieee754 = require('ieee754')
var isArray = require('isarray')

exports.Buffer = Buffer
exports.SlowBuffer = SlowBuffer
exports.INSPECT_MAX_BYTES = 50
Buffer.poolSize = 8192 // not used by this implementation

var rootParent = {}

/**
 * If `Buffer.TYPED_ARRAY_SUPPORT`:
 *   === true    Use Uint8Array implementation (fastest)
 *   === false   Use Object implementation (most compatible, even IE6)
 *
 * Browsers that support typed arrays are IE 10+, Firefox 4+, Chrome 7+, Safari 5.1+,
 * Opera 11.6+, iOS 4.2+.
 *
 * Due to various browser bugs, sometimes the Object implementation will be used even
 * when the browser supports typed arrays.
 *
 * Note:
 *
 *   - Firefox 4-29 lacks support for adding new properties to `Uint8Array` instances,
 *     See: https://bugzilla.mozilla.org/show_bug.cgi?id=695438.
 *
 *   - Chrome 9-10 is missing the `TypedArray.prototype.subarray` function.
 *
 *   - IE10 has a broken `TypedArray.prototype.subarray` function which returns arrays of
 *     incorrect length in some situations.

 * We detect these buggy browsers and set `Buffer.TYPED_ARRAY_SUPPORT` to `false` so they
 * get the Object implementation, which is slower but behaves correctly.
 */
Buffer.TYPED_ARRAY_SUPPORT = global.TYPED_ARRAY_SUPPORT !== undefined
  ? global.TYPED_ARRAY_SUPPORT
  : typedArraySupport()

function typedArraySupport () {
  try {
    var arr = new Uint8Array(1)
    arr.foo = function () { return 42 }
    return arr.foo() === 42 && // typed array instances can be augmented
        typeof arr.subarray === 'function' && // chrome 9-10 lack `subarray`
        arr.subarray(1, 1).byteLength === 0 // ie10 has broken `subarray`
  } catch (e) {
    return false
  }
}

function kMaxLength () {
  return Buffer.TYPED_ARRAY_SUPPORT
    ? 0x7fffffff
    : 0x3fffffff
}

/**
 * Class: Buffer
 * =============
 *
 * The Buffer constructor returns instances of `Uint8Array` that are augmented
 * with function properties for all the node `Buffer` API functions. We use
 * `Uint8Array` so that square bracket notation works as expected -- it returns
 * a single octet.
 *
 * By augmenting the instances, we can avoid modifying the `Uint8Array`
 * prototype.
 */
function Buffer (arg) {
  if (!(this instanceof Buffer)) {
    // Avoid going through an ArgumentsAdaptorTrampoline in the common case.
    if (arguments.length > 1) return new Buffer(arg, arguments[1])
    return new Buffer(arg)
  }

  if (!Buffer.TYPED_ARRAY_SUPPORT) {
    this.length = 0
    this.parent = undefined
  }

  // Common case.
  if (typeof arg === 'number') {
    return fromNumber(this, arg)
  }

  // Slightly less common case.
  if (typeof arg === 'string') {
    return fromString(this, arg, arguments.length > 1 ? arguments[1] : 'utf8')
  }

  // Unusual.
  return fromObject(this, arg)
}

function fromNumber (that, length) {
  that = allocate(that, length < 0 ? 0 : checked(length) | 0)
  if (!Buffer.TYPED_ARRAY_SUPPORT) {
    for (var i = 0; i < length; i++) {
      that[i] = 0
    }
  }
  return that
}

function fromString (that, string, encoding) {
  if (typeof encoding !== 'string' || encoding === '') encoding = 'utf8'

  // Assumption: byteLength() return value is always < kMaxLength.
  var length = byteLength(string, encoding) | 0
  that = allocate(that, length)

  that.write(string, encoding)
  return that
}

function fromObject (that, object) {
  if (Buffer.isBuffer(object)) return fromBuffer(that, object)

  if (isArray(object)) return fromArray(that, object)

  if (object == null) {
    throw new TypeError('must start with number, buffer, array or string')
  }

  if (typeof ArrayBuffer !== 'undefined') {
    if (object.buffer instanceof ArrayBuffer) {
      return fromTypedArray(that, object)
    }
    if (object instanceof ArrayBuffer) {
      return fromArrayBuffer(that, object)
    }
  }

  if (object.length) return fromArrayLike(that, object)

  return fromJsonObject(that, object)
}

function fromBuffer (that, buffer) {
  var length = checked(buffer.length) | 0
  that = allocate(that, length)
  buffer.copy(that, 0, 0, length)
  return that
}

function fromArray (that, array) {
  var length = checked(array.length) | 0
  that = allocate(that, length)
  for (var i = 0; i < length; i += 1) {
    that[i] = array[i] & 255
  }
  return that
}

// Duplicate of fromArray() to keep fromArray() monomorphic.
function fromTypedArray (that, array) {
  var length = checked(array.length) | 0
  that = allocate(that, length)
  // Truncating the elements is probably not what people expect from typed
  // arrays with BYTES_PER_ELEMENT > 1 but it's compatible with the behavior
  // of the old Buffer constructor.
  for (var i = 0; i < length; i += 1) {
    that[i] = array[i] & 255
  }
  return that
}

function fromArrayBuffer (that, array) {
  array.byteLength // this throws if `array` is not a valid ArrayBuffer

  if (Buffer.TYPED_ARRAY_SUPPORT) {
    // Return an augmented `Uint8Array` instance, for best performance
    that = new Uint8Array(array)
    that.__proto__ = Buffer.prototype
  } else {
    // Fallback: Return an object instance of the Buffer class
    that = fromTypedArray(that, new Uint8Array(array))
  }
  return that
}

function fromArrayLike (that, array) {
  var length = checked(array.length) | 0
  that = allocate(that, length)
  for (var i = 0; i < length; i += 1) {
    that[i] = array[i] & 255
  }
  return that
}

// Deserialize { type: 'Buffer', data: [1,2,3,...] } into a Buffer object.
// Returns a zero-length buffer for inputs that don't conform to the spec.
function fromJsonObject (that, object) {
  var array
  var length = 0

  if (object.type === 'Buffer' && isArray(object.data)) {
    array = object.data
    length = checked(array.length) | 0
  }
  that = allocate(that, length)

  for (var i = 0; i < length; i += 1) {
    that[i] = array[i] & 255
  }
  return that
}

if (Buffer.TYPED_ARRAY_SUPPORT) {
  Buffer.prototype.__proto__ = Uint8Array.prototype
  Buffer.__proto__ = Uint8Array
} else {
  // pre-set for values that may exist in the future
  Buffer.prototype.length = undefined
  Buffer.prototype.parent = undefined
}

function allocate (that, length) {
  if (Buffer.TYPED_ARRAY_SUPPORT) {
    // Return an augmented `Uint8Array` instance, for best performance
    that = new Uint8Array(length)
    that.__proto__ = Buffer.prototype
  } else {
    // Fallback: Return an object instance of the Buffer class
    that.length = length
  }

  var fromPool = length !== 0 && length <= Buffer.poolSize >>> 1
  if (fromPool) that.parent = rootParent

  return that
}

function checked (length) {
  // Note: cannot use `length < kMaxLength` here because that fails when
  // length is NaN (which is otherwise coerced to zero.)
  if (length >= kMaxLength()) {
    throw new RangeError('Attempt to allocate Buffer larger than maximum ' +
                         'size: 0x' + kMaxLength().toString(16) + ' bytes')
  }
  return length | 0
}

function SlowBuffer (subject, encoding) {
  if (!(this instanceof SlowBuffer)) return new SlowBuffer(subject, encoding)

  var buf = new Buffer(subject, encoding)
  delete buf.parent
  return buf
}

Buffer.isBuffer = function isBuffer (b) {
  return !!(b != null && b._isBuffer)
}

Buffer.compare = function compare (a, b) {
  if (!Buffer.isBuffer(a) || !Buffer.isBuffer(b)) {
    throw new TypeError('Arguments must be Buffers')
  }

  if (a === b) return 0

  var x = a.length
  var y = b.length

  var i = 0
  var len = Math.min(x, y)
  while (i < len) {
    if (a[i] !== b[i]) break

    ++i
  }

  if (i !== len) {
    x = a[i]
    y = b[i]
  }

  if (x < y) return -1
  if (y < x) return 1
  return 0
}

Buffer.isEncoding = function isEncoding (encoding) {
  switch (String(encoding).toLowerCase()) {
    case 'hex':
    case 'utf8':
    case 'utf-8':
    case 'ascii':
    case 'binary':
    case 'base64':
    case 'raw':
    case 'ucs2':
    case 'ucs-2':
    case 'utf16le':
    case 'utf-16le':
      return true
    default:
      return false
  }
}

Buffer.concat = function concat (list, length) {
  if (!isArray(list)) throw new TypeError('list argument must be an Array of Buffers.')

  if (list.length === 0) {
    return new Buffer(0)
  }

  var i
  if (length === undefined) {
    length = 0
    for (i = 0; i < list.length; i++) {
      length += list[i].length
    }
  }

  var buf = new Buffer(length)
  var pos = 0
  for (i = 0; i < list.length; i++) {
    var item = list[i]
    item.copy(buf, pos)
    pos += item.length
  }
  return buf
}

function byteLength (string, encoding) {
  if (typeof string !== 'string') string = '' + string

  var len = string.length
  if (len === 0) return 0

  // Use a for loop to avoid recursion
  var loweredCase = false
  for (;;) {
    switch (encoding) {
      case 'ascii':
      case 'binary':
      // Deprecated
      case 'raw':
      case 'raws':
        return len
      case 'utf8':
      case 'utf-8':
        return utf8ToBytes(string).length
      case 'ucs2':
      case 'ucs-2':
      case 'utf16le':
      case 'utf-16le':
        return len * 2
      case 'hex':
        return len >>> 1
      case 'base64':
        return base64ToBytes(string).length
      default:
        if (loweredCase) return utf8ToBytes(string).length // assume utf8
        encoding = ('' + encoding).toLowerCase()
        loweredCase = true
    }
  }
}
Buffer.byteLength = byteLength

function slowToString (encoding, start, end) {
  var loweredCase = false

  start = start | 0
  end = end === undefined || end === Infinity ? this.length : end | 0

  if (!encoding) encoding = 'utf8'
  if (start < 0) start = 0
  if (end > this.length) end = this.length
  if (end <= start) return ''

  while (true) {
    switch (encoding) {
      case 'hex':
        return hexSlice(this, start, end)

      case 'utf8':
      case 'utf-8':
        return utf8Slice(this, start, end)

      case 'ascii':
        return asciiSlice(this, start, end)

      case 'binary':
        return binarySlice(this, start, end)

      case 'base64':
        return base64Slice(this, start, end)

      case 'ucs2':
      case 'ucs-2':
      case 'utf16le':
      case 'utf-16le':
        return utf16leSlice(this, start, end)

      default:
        if (loweredCase) throw new TypeError('Unknown encoding: ' + encoding)
        encoding = (encoding + '').toLowerCase()
        loweredCase = true
    }
  }
}

// Even though this property is private, it shouldn't be removed because it is
// used by `is-buffer` to detect buffer instances in Safari 5-7.
Buffer.prototype._isBuffer = true

Buffer.prototype.toString = function toString () {
  var length = this.length | 0
  if (length === 0) return ''
  if (arguments.length === 0) return utf8Slice(this, 0, length)
  return slowToString.apply(this, arguments)
}

Buffer.prototype.equals = function equals (b) {
  if (!Buffer.isBuffer(b)) throw new TypeError('Argument must be a Buffer')
  if (this === b) return true
  return Buffer.compare(this, b) === 0
}

Buffer.prototype.inspect = function inspect () {
  var str = ''
  var max = exports.INSPECT_MAX_BYTES
  if (this.length > 0) {
    str = this.toString('hex', 0, max).match(/.{2}/g).join(' ')
    if (this.length > max) str += ' ... '
  }
  return '<Buffer ' + str + '>'
}

Buffer.prototype.compare = function compare (b) {
  if (!Buffer.isBuffer(b)) throw new TypeError('Argument must be a Buffer')
  if (this === b) return 0
  return Buffer.compare(this, b)
}

Buffer.prototype.indexOf = function indexOf (val, byteOffset) {
  if (byteOffset > 0x7fffffff) byteOffset = 0x7fffffff
  else if (byteOffset < -0x80000000) byteOffset = -0x80000000
  byteOffset >>= 0

  if (this.length === 0) return -1
  if (byteOffset >= this.length) return -1

  // Negative offsets start from the end of the buffer
  if (byteOffset < 0) byteOffset = Math.max(this.length + byteOffset, 0)

  if (typeof val === 'string') {
    if (val.length === 0) return -1 // special case: looking for empty string always fails
    return String.prototype.indexOf.call(this, val, byteOffset)
  }
  if (Buffer.isBuffer(val)) {
    return arrayIndexOf(this, val, byteOffset)
  }
  if (typeof val === 'number') {
    if (Buffer.TYPED_ARRAY_SUPPORT && Uint8Array.prototype.indexOf === 'function') {
      return Uint8Array.prototype.indexOf.call(this, val, byteOffset)
    }
    return arrayIndexOf(this, [ val ], byteOffset)
  }

  function arrayIndexOf (arr, val, byteOffset) {
    var foundIndex = -1
    for (var i = 0; byteOffset + i < arr.length; i++) {
      if (arr[byteOffset + i] === val[foundIndex === -1 ? 0 : i - foundIndex]) {
        if (foundIndex === -1) foundIndex = i
        if (i - foundIndex + 1 === val.length) return byteOffset + foundIndex
      } else {
        foundIndex = -1
      }
    }
    return -1
  }

  throw new TypeError('val must be string, number or Buffer')
}

function hexWrite (buf, string, offset, length) {
  offset = Number(offset) || 0
  var remaining = buf.length - offset
  if (!length) {
    length = remaining
  } else {
    length = Number(length)
    if (length > remaining) {
      length = remaining
    }
  }

  // must be an even number of digits
  var strLen = string.length
  if (strLen % 2 !== 0) throw new Error('Invalid hex string')

  if (length > strLen / 2) {
    length = strLen / 2
  }
  for (var i = 0; i < length; i++) {
    var parsed = parseInt(string.substr(i * 2, 2), 16)
    if (isNaN(parsed)) throw new Error('Invalid hex string')
    buf[offset + i] = parsed
  }
  return i
}

function utf8Write (buf, string, offset, length) {
  return blitBuffer(utf8ToBytes(string, buf.length - offset), buf, offset, length)
}

function asciiWrite (buf, string, offset, length) {
  return blitBuffer(asciiToBytes(string), buf, offset, length)
}

function binaryWrite (buf, string, offset, length) {
  return asciiWrite(buf, string, offset, length)
}

function base64Write (buf, string, offset, length) {
  return blitBuffer(base64ToBytes(string), buf, offset, length)
}

function ucs2Write (buf, string, offset, length) {
  return blitBuffer(utf16leToBytes(string, buf.length - offset), buf, offset, length)
}

Buffer.prototype.write = function write (string, offset, length, encoding) {
  // Buffer#write(string)
  if (offset === undefined) {
    encoding = 'utf8'
    length = this.length
    offset = 0
  // Buffer#write(string, encoding)
  } else if (length === undefined && typeof offset === 'string') {
    encoding = offset
    length = this.length
    offset = 0
  // Buffer#write(string, offset[, length][, encoding])
  } else if (isFinite(offset)) {
    offset = offset | 0
    if (isFinite(length)) {
      length = length | 0
      if (encoding === undefined) encoding = 'utf8'
    } else {
      encoding = length
      length = undefined
    }
  // legacy write(string, encoding, offset, length) - remove in v0.13
  } else {
    var swap = encoding
    encoding = offset
    offset = length | 0
    length = swap
  }

  var remaining = this.length - offset
  if (length === undefined || length > remaining) length = remaining

  if ((string.length > 0 && (length < 0 || offset < 0)) || offset > this.length) {
    throw new RangeError('attempt to write outside buffer bounds')
  }

  if (!encoding) encoding = 'utf8'

  var loweredCase = false
  for (;;) {
    switch (encoding) {
      case 'hex':
        return hexWrite(this, string, offset, length)

      case 'utf8':
      case 'utf-8':
        return utf8Write(this, string, offset, length)

      case 'ascii':
        return asciiWrite(this, string, offset, length)

      case 'binary':
        return binaryWrite(this, string, offset, length)

      case 'base64':
        // Warning: maxLength not taken into account in base64Write
        return base64Write(this, string, offset, length)

      case 'ucs2':
      case 'ucs-2':
      case 'utf16le':
      case 'utf-16le':
        return ucs2Write(this, string, offset, length)

      default:
        if (loweredCase) throw new TypeError('Unknown encoding: ' + encoding)
        encoding = ('' + encoding).toLowerCase()
        loweredCase = true
    }
  }
}

Buffer.prototype.toJSON = function toJSON () {
  return {
    type: 'Buffer',
    data: Array.prototype.slice.call(this._arr || this, 0)
  }
}

function base64Slice (buf, start, end) {
  if (start === 0 && end === buf.length) {
    return base64.fromByteArray(buf)
  } else {
    return base64.fromByteArray(buf.slice(start, end))
  }
}

function utf8Slice (buf, start, end) {
  end = Math.min(buf.length, end)
  var res = []

  var i = start
  while (i < end) {
    var firstByte = buf[i]
    var codePoint = null
    var bytesPerSequence = (firstByte > 0xEF) ? 4
      : (firstByte > 0xDF) ? 3
      : (firstByte > 0xBF) ? 2
      : 1

    if (i + bytesPerSequence <= end) {
      var secondByte, thirdByte, fourthByte, tempCodePoint

      switch (bytesPerSequence) {
        case 1:
          if (firstByte < 0x80) {
            codePoint = firstByte
          }
          break
        case 2:
          secondByte = buf[i + 1]
          if ((secondByte & 0xC0) === 0x80) {
            tempCodePoint = (firstByte & 0x1F) << 0x6 | (secondByte & 0x3F)
            if (tempCodePoint > 0x7F) {
              codePoint = tempCodePoint
            }
          }
          break
        case 3:
          secondByte = buf[i + 1]
          thirdByte = buf[i + 2]
          if ((secondByte & 0xC0) === 0x80 && (thirdByte & 0xC0) === 0x80) {
            tempCodePoint = (firstByte & 0xF) << 0xC | (secondByte & 0x3F) << 0x6 | (thirdByte & 0x3F)
            if (tempCodePoint > 0x7FF && (tempCodePoint < 0xD800 || tempCodePoint > 0xDFFF)) {
              codePoint = tempCodePoint
            }
          }
          break
        case 4:
          secondByte = buf[i + 1]
          thirdByte = buf[i + 2]
          fourthByte = buf[i + 3]
          if ((secondByte & 0xC0) === 0x80 && (thirdByte & 0xC0) === 0x80 && (fourthByte & 0xC0) === 0x80) {
            tempCodePoint = (firstByte & 0xF) << 0x12 | (secondByte & 0x3F) << 0xC | (thirdByte & 0x3F) << 0x6 | (fourthByte & 0x3F)
            if (tempCodePoint > 0xFFFF && tempCodePoint < 0x110000) {
              codePoint = tempCodePoint
            }
          }
      }
    }

    if (codePoint === null) {
      // we did not generate a valid codePoint so insert a
      // replacement char (U+FFFD) and advance only 1 byte
      codePoint = 0xFFFD
      bytesPerSequence = 1
    } else if (codePoint > 0xFFFF) {
      // encode to utf16 (surrogate pair dance)
      codePoint -= 0x10000
      res.push(codePoint >>> 10 & 0x3FF | 0xD800)
      codePoint = 0xDC00 | codePoint & 0x3FF
    }

    res.push(codePoint)
    i += bytesPerSequence
  }

  return decodeCodePointsArray(res)
}

// Based on http://stackoverflow.com/a/22747272/680742, the browser with
// the lowest limit is Chrome, with 0x10000 args.
// We go 1 magnitude less, for safety
var MAX_ARGUMENTS_LENGTH = 0x1000

function decodeCodePointsArray (codePoints) {
  var len = codePoints.length
  if (len <= MAX_ARGUMENTS_LENGTH) {
    return String.fromCharCode.apply(String, codePoints) // avoid extra slice()
  }

  // Decode in chunks to avoid "call stack size exceeded".
  var res = ''
  var i = 0
  while (i < len) {
    res += String.fromCharCode.apply(
      String,
      codePoints.slice(i, i += MAX_ARGUMENTS_LENGTH)
    )
  }
  return res
}

function asciiSlice (buf, start, end) {
  var ret = ''
  end = Math.min(buf.length, end)

  for (var i = start; i < end; i++) {
    ret += String.fromCharCode(buf[i] & 0x7F)
  }
  return ret
}

function binarySlice (buf, start, end) {
  var ret = ''
  end = Math.min(buf.length, end)

  for (var i = start; i < end; i++) {
    ret += String.fromCharCode(buf[i])
  }
  return ret
}

function hexSlice (buf, start, end) {
  var len = buf.length

  if (!start || start < 0) start = 0
  if (!end || end < 0 || end > len) end = len

  var out = ''
  for (var i = start; i < end; i++) {
    out += toHex(buf[i])
  }
  return out
}

function utf16leSlice (buf, start, end) {
  var bytes = buf.slice(start, end)
  var res = ''
  for (var i = 0; i < bytes.length; i += 2) {
    res += String.fromCharCode(bytes[i] + bytes[i + 1] * 256)
  }
  return res
}

Buffer.prototype.slice = function slice (start, end) {
  var len = this.length
  start = ~~start
  end = end === undefined ? len : ~~end

  if (start < 0) {
    start += len
    if (start < 0) start = 0
  } else if (start > len) {
    start = len
  }

  if (end < 0) {
    end += len
    if (end < 0) end = 0
  } else if (end > len) {
    end = len
  }

  if (end < start) end = start

  var newBuf
  if (Buffer.TYPED_ARRAY_SUPPORT) {
    newBuf = this.subarray(start, end)
    newBuf.__proto__ = Buffer.prototype
  } else {
    var sliceLen = end - start
    newBuf = new Buffer(sliceLen, undefined)
    for (var i = 0; i < sliceLen; i++) {
      newBuf[i] = this[i + start]
    }
  }

  if (newBuf.length) newBuf.parent = this.parent || this

  return newBuf
}

/*
 * Need to make sure that buffer isn't trying to write out of bounds.
 */
function checkOffset (offset, ext, length) {
  if ((offset % 1) !== 0 || offset < 0) throw new RangeError('offset is not uint')
  if (offset + ext > length) throw new RangeError('Trying to access beyond buffer length')
}

Buffer.prototype.readUIntLE = function readUIntLE (offset, byteLength, noAssert) {
  offset = offset | 0
  byteLength = byteLength | 0
  if (!noAssert) checkOffset(offset, byteLength, this.length)

  var val = this[offset]
  var mul = 1
  var i = 0
  while (++i < byteLength && (mul *= 0x100)) {
    val += this[offset + i] * mul
  }

  return val
}

Buffer.prototype.readUIntBE = function readUIntBE (offset, byteLength, noAssert) {
  offset = offset | 0
  byteLength = byteLength | 0
  if (!noAssert) {
    checkOffset(offset, byteLength, this.length)
  }

  var val = this[offset + --byteLength]
  var mul = 1
  while (byteLength > 0 && (mul *= 0x100)) {
    val += this[offset + --byteLength] * mul
  }

  return val
}

Buffer.prototype.readUInt8 = function readUInt8 (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 1, this.length)
  return this[offset]
}

Buffer.prototype.readUInt16LE = function readUInt16LE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 2, this.length)
  return this[offset] | (this[offset + 1] << 8)
}

Buffer.prototype.readUInt16BE = function readUInt16BE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 2, this.length)
  return (this[offset] << 8) | this[offset + 1]
}

Buffer.prototype.readUInt32LE = function readUInt32LE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 4, this.length)

  return ((this[offset]) |
      (this[offset + 1] << 8) |
      (this[offset + 2] << 16)) +
      (this[offset + 3] * 0x1000000)
}

Buffer.prototype.readUInt32BE = function readUInt32BE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 4, this.length)

  return (this[offset] * 0x1000000) +
    ((this[offset + 1] << 16) |
    (this[offset + 2] << 8) |
    this[offset + 3])
}

Buffer.prototype.readIntLE = function readIntLE (offset, byteLength, noAssert) {
  offset = offset | 0
  byteLength = byteLength | 0
  if (!noAssert) checkOffset(offset, byteLength, this.length)

  var val = this[offset]
  var mul = 1
  var i = 0
  while (++i < byteLength && (mul *= 0x100)) {
    val += this[offset + i] * mul
  }
  mul *= 0x80

  if (val >= mul) val -= Math.pow(2, 8 * byteLength)

  return val
}

Buffer.prototype.readIntBE = function readIntBE (offset, byteLength, noAssert) {
  offset = offset | 0
  byteLength = byteLength | 0
  if (!noAssert) checkOffset(offset, byteLength, this.length)

  var i = byteLength
  var mul = 1
  var val = this[offset + --i]
  while (i > 0 && (mul *= 0x100)) {
    val += this[offset + --i] * mul
  }
  mul *= 0x80

  if (val >= mul) val -= Math.pow(2, 8 * byteLength)

  return val
}

Buffer.prototype.readInt8 = function readInt8 (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 1, this.length)
  if (!(this[offset] & 0x80)) return (this[offset])
  return ((0xff - this[offset] + 1) * -1)
}

Buffer.prototype.readInt16LE = function readInt16LE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 2, this.length)
  var val = this[offset] | (this[offset + 1] << 8)
  return (val & 0x8000) ? val | 0xFFFF0000 : val
}

Buffer.prototype.readInt16BE = function readInt16BE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 2, this.length)
  var val = this[offset + 1] | (this[offset] << 8)
  return (val & 0x8000) ? val | 0xFFFF0000 : val
}

Buffer.prototype.readInt32LE = function readInt32LE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 4, this.length)

  return (this[offset]) |
    (this[offset + 1] << 8) |
    (this[offset + 2] << 16) |
    (this[offset + 3] << 24)
}

Buffer.prototype.readInt32BE = function readInt32BE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 4, this.length)

  return (this[offset] << 24) |
    (this[offset + 1] << 16) |
    (this[offset + 2] << 8) |
    (this[offset + 3])
}

Buffer.prototype.readFloatLE = function readFloatLE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 4, this.length)
  return ieee754.read(this, offset, true, 23, 4)
}

Buffer.prototype.readFloatBE = function readFloatBE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 4, this.length)
  return ieee754.read(this, offset, false, 23, 4)
}

Buffer.prototype.readDoubleLE = function readDoubleLE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 8, this.length)
  return ieee754.read(this, offset, true, 52, 8)
}

Buffer.prototype.readDoubleBE = function readDoubleBE (offset, noAssert) {
  if (!noAssert) checkOffset(offset, 8, this.length)
  return ieee754.read(this, offset, false, 52, 8)
}

function checkInt (buf, value, offset, ext, max, min) {
  if (!Buffer.isBuffer(buf)) throw new TypeError('buffer must be a Buffer instance')
  if (value > max || value < min) throw new RangeError('value is out of bounds')
  if (offset + ext > buf.length) throw new RangeError('index out of range')
}

Buffer.prototype.writeUIntLE = function writeUIntLE (value, offset, byteLength, noAssert) {
  value = +value
  offset = offset | 0
  byteLength = byteLength | 0
  if (!noAssert) checkInt(this, value, offset, byteLength, Math.pow(2, 8 * byteLength), 0)

  var mul = 1
  var i = 0
  this[offset] = value & 0xFF
  while (++i < byteLength && (mul *= 0x100)) {
    this[offset + i] = (value / mul) & 0xFF
  }

  return offset + byteLength
}

Buffer.prototype.writeUIntBE = function writeUIntBE (value, offset, byteLength, noAssert) {
  value = +value
  offset = offset | 0
  byteLength = byteLength | 0
  if (!noAssert) checkInt(this, value, offset, byteLength, Math.pow(2, 8 * byteLength), 0)

  var i = byteLength - 1
  var mul = 1
  this[offset + i] = value & 0xFF
  while (--i >= 0 && (mul *= 0x100)) {
    this[offset + i] = (value / mul) & 0xFF
  }

  return offset + byteLength
}

Buffer.prototype.writeUInt8 = function writeUInt8 (value, offset, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) checkInt(this, value, offset, 1, 0xff, 0)
  if (!Buffer.TYPED_ARRAY_SUPPORT) value = Math.floor(value)
  this[offset] = (value & 0xff)
  return offset + 1
}

function objectWriteUInt16 (buf, value, offset, littleEndian) {
  if (value < 0) value = 0xffff + value + 1
  for (var i = 0, j = Math.min(buf.length - offset, 2); i < j; i++) {
    buf[offset + i] = (value & (0xff << (8 * (littleEndian ? i : 1 - i)))) >>>
      (littleEndian ? i : 1 - i) * 8
  }
}

Buffer.prototype.writeUInt16LE = function writeUInt16LE (value, offset, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) checkInt(this, value, offset, 2, 0xffff, 0)
  if (Buffer.TYPED_ARRAY_SUPPORT) {
    this[offset] = (value & 0xff)
    this[offset + 1] = (value >>> 8)
  } else {
    objectWriteUInt16(this, value, offset, true)
  }
  return offset + 2
}

Buffer.prototype.writeUInt16BE = function writeUInt16BE (value, offset, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) checkInt(this, value, offset, 2, 0xffff, 0)
  if (Buffer.TYPED_ARRAY_SUPPORT) {
    this[offset] = (value >>> 8)
    this[offset + 1] = (value & 0xff)
  } else {
    objectWriteUInt16(this, value, offset, false)
  }
  return offset + 2
}

function objectWriteUInt32 (buf, value, offset, littleEndian) {
  if (value < 0) value = 0xffffffff + value + 1
  for (var i = 0, j = Math.min(buf.length - offset, 4); i < j; i++) {
    buf[offset + i] = (value >>> (littleEndian ? i : 3 - i) * 8) & 0xff
  }
}

Buffer.prototype.writeUInt32LE = function writeUInt32LE (value, offset, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) checkInt(this, value, offset, 4, 0xffffffff, 0)
  if (Buffer.TYPED_ARRAY_SUPPORT) {
    this[offset + 3] = (value >>> 24)
    this[offset + 2] = (value >>> 16)
    this[offset + 1] = (value >>> 8)
    this[offset] = (value & 0xff)
  } else {
    objectWriteUInt32(this, value, offset, true)
  }
  return offset + 4
}

Buffer.prototype.writeUInt32BE = function writeUInt32BE (value, offset, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) checkInt(this, value, offset, 4, 0xffffffff, 0)
  if (Buffer.TYPED_ARRAY_SUPPORT) {
    this[offset] = (value >>> 24)
    this[offset + 1] = (value >>> 16)
    this[offset + 2] = (value >>> 8)
    this[offset + 3] = (value & 0xff)
  } else {
    objectWriteUInt32(this, value, offset, false)
  }
  return offset + 4
}

Buffer.prototype.writeIntLE = function writeIntLE (value, offset, byteLength, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) {
    var limit = Math.pow(2, 8 * byteLength - 1)

    checkInt(this, value, offset, byteLength, limit - 1, -limit)
  }

  var i = 0
  var mul = 1
  var sub = value < 0 ? 1 : 0
  this[offset] = value & 0xFF
  while (++i < byteLength && (mul *= 0x100)) {
    this[offset + i] = ((value / mul) >> 0) - sub & 0xFF
  }

  return offset + byteLength
}

Buffer.prototype.writeIntBE = function writeIntBE (value, offset, byteLength, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) {
    var limit = Math.pow(2, 8 * byteLength - 1)

    checkInt(this, value, offset, byteLength, limit - 1, -limit)
  }

  var i = byteLength - 1
  var mul = 1
  var sub = value < 0 ? 1 : 0
  this[offset + i] = value & 0xFF
  while (--i >= 0 && (mul *= 0x100)) {
    this[offset + i] = ((value / mul) >> 0) - sub & 0xFF
  }

  return offset + byteLength
}

Buffer.prototype.writeInt8 = function writeInt8 (value, offset, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) checkInt(this, value, offset, 1, 0x7f, -0x80)
  if (!Buffer.TYPED_ARRAY_SUPPORT) value = Math.floor(value)
  if (value < 0) value = 0xff + value + 1
  this[offset] = (value & 0xff)
  return offset + 1
}

Buffer.prototype.writeInt16LE = function writeInt16LE (value, offset, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) checkInt(this, value, offset, 2, 0x7fff, -0x8000)
  if (Buffer.TYPED_ARRAY_SUPPORT) {
    this[offset] = (value & 0xff)
    this[offset + 1] = (value >>> 8)
  } else {
    objectWriteUInt16(this, value, offset, true)
  }
  return offset + 2
}

Buffer.prototype.writeInt16BE = function writeInt16BE (value, offset, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) checkInt(this, value, offset, 2, 0x7fff, -0x8000)
  if (Buffer.TYPED_ARRAY_SUPPORT) {
    this[offset] = (value >>> 8)
    this[offset + 1] = (value & 0xff)
  } else {
    objectWriteUInt16(this, value, offset, false)
  }
  return offset + 2
}

Buffer.prototype.writeInt32LE = function writeInt32LE (value, offset, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) checkInt(this, value, offset, 4, 0x7fffffff, -0x80000000)
  if (Buffer.TYPED_ARRAY_SUPPORT) {
    this[offset] = (value & 0xff)
    this[offset + 1] = (value >>> 8)
    this[offset + 2] = (value >>> 16)
    this[offset + 3] = (value >>> 24)
  } else {
    objectWriteUInt32(this, value, offset, true)
  }
  return offset + 4
}

Buffer.prototype.writeInt32BE = function writeInt32BE (value, offset, noAssert) {
  value = +value
  offset = offset | 0
  if (!noAssert) checkInt(this, value, offset, 4, 0x7fffffff, -0x80000000)
  if (value < 0) value = 0xffffffff + value + 1
  if (Buffer.TYPED_ARRAY_SUPPORT) {
    this[offset] = (value >>> 24)
    this[offset + 1] = (value >>> 16)
    this[offset + 2] = (value >>> 8)
    this[offset + 3] = (value & 0xff)
  } else {
    objectWriteUInt32(this, value, offset, false)
  }
  return offset + 4
}

function checkIEEE754 (buf, value, offset, ext, max, min) {
  if (value > max || value < min) throw new RangeError('value is out of bounds')
  if (offset + ext > buf.length) throw new RangeError('index out of range')
  if (offset < 0) throw new RangeError('index out of range')
}

function writeFloat (buf, value, offset, littleEndian, noAssert) {
  if (!noAssert) {
    checkIEEE754(buf, value, offset, 4, 3.4028234663852886e+38, -3.4028234663852886e+38)
  }
  ieee754.write(buf, value, offset, littleEndian, 23, 4)
  return offset + 4
}

Buffer.prototype.writeFloatLE = function writeFloatLE (value, offset, noAssert) {
  return writeFloat(this, value, offset, true, noAssert)
}

Buffer.prototype.writeFloatBE = function writeFloatBE (value, offset, noAssert) {
  return writeFloat(this, value, offset, false, noAssert)
}

function writeDouble (buf, value, offset, littleEndian, noAssert) {
  if (!noAssert) {
    checkIEEE754(buf, value, offset, 8, 1.7976931348623157E+308, -1.7976931348623157E+308)
  }
  ieee754.write(buf, value, offset, littleEndian, 52, 8)
  return offset + 8
}

Buffer.prototype.writeDoubleLE = function writeDoubleLE (value, offset, noAssert) {
  return writeDouble(this, value, offset, true, noAssert)
}

Buffer.prototype.writeDoubleBE = function writeDoubleBE (value, offset, noAssert) {
  return writeDouble(this, value, offset, false, noAssert)
}

// copy(targetBuffer, targetStart=0, sourceStart=0, sourceEnd=buffer.length)
Buffer.prototype.copy = function copy (target, targetStart, start, end) {
  if (!start) start = 0
  if (!end && end !== 0) end = this.length
  if (targetStart >= target.length) targetStart = target.length
  if (!targetStart) targetStart = 0
  if (end > 0 && end < start) end = start

  // Copy 0 bytes; we're done
  if (end === start) return 0
  if (target.length === 0 || this.length === 0) return 0

  // Fatal error conditions
  if (targetStart < 0) {
    throw new RangeError('targetStart out of bounds')
  }
  if (start < 0 || start >= this.length) throw new RangeError('sourceStart out of bounds')
  if (end < 0) throw new RangeError('sourceEnd out of bounds')

  // Are we oob?
  if (end > this.length) end = this.length
  if (target.length - targetStart < end - start) {
    end = target.length - targetStart + start
  }

  var len = end - start
  var i

  if (this === target && start < targetStart && targetStart < end) {
    // descending copy from end
    for (i = len - 1; i >= 0; i--) {
      target[i + targetStart] = this[i + start]
    }
  } else if (len < 1000 || !Buffer.TYPED_ARRAY_SUPPORT) {
    // ascending copy from start
    for (i = 0; i < len; i++) {
      target[i + targetStart] = this[i + start]
    }
  } else {
    Uint8Array.prototype.set.call(
      target,
      this.subarray(start, start + len),
      targetStart
    )
  }

  return len
}

// fill(value, start=0, end=buffer.length)
Buffer.prototype.fill = function fill (value, start, end) {
  if (!value) value = 0
  if (!start) start = 0
  if (!end) end = this.length

  if (end < start) throw new RangeError('end < start')

  // Fill 0 bytes; we're done
  if (end === start) return
  if (this.length === 0) return

  if (start < 0 || start >= this.length) throw new RangeError('start out of bounds')
  if (end < 0 || end > this.length) throw new RangeError('end out of bounds')

  var i
  if (typeof value === 'number') {
    for (i = start; i < end; i++) {
      this[i] = value
    }
  } else {
    var bytes = utf8ToBytes(value.toString())
    var len = bytes.length
    for (i = start; i < end; i++) {
      this[i] = bytes[i % len]
    }
  }

  return this
}

// HELPER FUNCTIONS
// ================

var INVALID_BASE64_RE = /[^+\/0-9A-Za-z-_]/g

function base64clean (str) {
  // Node strips out invalid characters like \n and \t from the string, base64-js does not
  str = stringtrim(str).replace(INVALID_BASE64_RE, '')
  // Node converts strings with length < 2 to ''
  if (str.length < 2) return ''
  // Node allows for non-padded base64 strings (missing trailing ===), base64-js does not
  while (str.length % 4 !== 0) {
    str = str + '='
  }
  return str
}

function stringtrim (str) {
  if (str.trim) return str.trim()
  return str.replace(/^\s+|\s+$/g, '')
}

function toHex (n) {
  if (n < 16) return '0' + n.toString(16)
  return n.toString(16)
}

function utf8ToBytes (string, units) {
  units = units || Infinity
  var codePoint
  var length = string.length
  var leadSurrogate = null
  var bytes = []

  for (var i = 0; i < length; i++) {
    codePoint = string.charCodeAt(i)

    // is surrogate component
    if (codePoint > 0xD7FF && codePoint < 0xE000) {
      // last char was a lead
      if (!leadSurrogate) {
        // no lead yet
        if (codePoint > 0xDBFF) {
          // unexpected trail
          if ((units -= 3) > -1) bytes.push(0xEF, 0xBF, 0xBD)
          continue
        } else if (i + 1 === length) {
          // unpaired lead
          if ((units -= 3) > -1) bytes.push(0xEF, 0xBF, 0xBD)
          continue
        }

        // valid lead
        leadSurrogate = codePoint

        continue
      }

      // 2 leads in a row
      if (codePoint < 0xDC00) {
        if ((units -= 3) > -1) bytes.push(0xEF, 0xBF, 0xBD)
        leadSurrogate = codePoint
        continue
      }

      // valid surrogate pair
      codePoint = (leadSurrogate - 0xD800 << 10 | codePoint - 0xDC00) + 0x10000
    } else if (leadSurrogate) {
      // valid bmp char, but last char was a lead
      if ((units -= 3) > -1) bytes.push(0xEF, 0xBF, 0xBD)
    }

    leadSurrogate = null

    // encode utf8
    if (codePoint < 0x80) {
      if ((units -= 1) < 0) break
      bytes.push(codePoint)
    } else if (codePoint < 0x800) {
      if ((units -= 2) < 0) break
      bytes.push(
        codePoint >> 0x6 | 0xC0,
        codePoint & 0x3F | 0x80
      )
    } else if (codePoint < 0x10000) {
      if ((units -= 3) < 0) break
      bytes.push(
        codePoint >> 0xC | 0xE0,
        codePoint >> 0x6 & 0x3F | 0x80,
        codePoint & 0x3F | 0x80
      )
    } else if (codePoint < 0x110000) {
      if ((units -= 4) < 0) break
      bytes.push(
        codePoint >> 0x12 | 0xF0,
        codePoint >> 0xC & 0x3F | 0x80,
        codePoint >> 0x6 & 0x3F | 0x80,
        codePoint & 0x3F | 0x80
      )
    } else {
      throw new Error('Invalid code point')
    }
  }

  return bytes
}

function asciiToBytes (str) {
  var byteArray = []
  for (var i = 0; i < str.length; i++) {
    // Node's code seems to be doing this and not & 0x7F..
    byteArray.push(str.charCodeAt(i) & 0xFF)
  }
  return byteArray
}

function utf16leToBytes (str, units) {
  var c, hi, lo
  var byteArray = []
  for (var i = 0; i < str.length; i++) {
    if ((units -= 2) < 0) break

    c = str.charCodeAt(i)
    hi = c >> 8
    lo = c % 256
    byteArray.push(lo)
    byteArray.push(hi)
  }

  return byteArray
}

function base64ToBytes (str) {
  return base64.toByteArray(base64clean(str))
}

function blitBuffer (src, dst, offset, length) {
  for (var i = 0; i < length; i++) {
    if ((i + offset >= dst.length) || (i >= src.length)) break
    dst[i + offset] = src[i]
  }
  return i
}

}).call(this,typeof global !== "undefined" ? global : typeof self !== "undefined" ? self : typeof window !== "undefined" ? window : {})
},{"base64-js":20,"ieee754":21,"isarray":22}],20:[function(require,module,exports){
;(function (exports) {
  'use strict'

  var lookup = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'

  var Arr = (typeof Uint8Array !== 'undefined')
    ? Uint8Array
    : Array

  var PLUS = '+'.charCodeAt(0)
  var SLASH = '/'.charCodeAt(0)
  var NUMBER = '0'.charCodeAt(0)
  var LOWER = 'a'.charCodeAt(0)
  var UPPER = 'A'.charCodeAt(0)
  var PLUS_URL_SAFE = '-'.charCodeAt(0)
  var SLASH_URL_SAFE = '_'.charCodeAt(0)

  function decode (elt) {
    var code = elt.charCodeAt(0)
    if (code === PLUS || code === PLUS_URL_SAFE) return 62 // '+'
    if (code === SLASH || code === SLASH_URL_SAFE) return 63 // '/'
    if (code < NUMBER) return -1 // no match
    if (code < NUMBER + 10) return code - NUMBER + 26 + 26
    if (code < UPPER + 26) return code - UPPER
    if (code < LOWER + 26) return code - LOWER + 26
  }

  function b64ToByteArray (b64) {
    var i, j, l, tmp, placeHolders, arr

    if (b64.length % 4 > 0) {
      throw new Error('Invalid string. Length must be a multiple of 4')
    }

    // the number of equal signs (place holders)
    // if there are two placeholders, than the two characters before it
    // represent one byte
    // if there is only one, then the three characters before it represent 2 bytes
    // this is just a cheap hack to not do indexOf twice
    var len = b64.length
    placeHolders = b64.charAt(len - 2) === '=' ? 2 : b64.charAt(len - 1) === '=' ? 1 : 0

    // base64 is 4/3 + up to two characters of the original data
    arr = new Arr(b64.length * 3 / 4 - placeHolders)

    // if there are placeholders, only get up to the last complete 4 chars
    l = placeHolders > 0 ? b64.length - 4 : b64.length

    var L = 0

    function push (v) {
      arr[L++] = v
    }

    for (i = 0, j = 0; i < l; i += 4, j += 3) {
      tmp = (decode(b64.charAt(i)) << 18) | (decode(b64.charAt(i + 1)) << 12) | (decode(b64.charAt(i + 2)) << 6) | decode(b64.charAt(i + 3))
      push((tmp & 0xFF0000) >> 16)
      push((tmp & 0xFF00) >> 8)
      push(tmp & 0xFF)
    }

    if (placeHolders === 2) {
      tmp = (decode(b64.charAt(i)) << 2) | (decode(b64.charAt(i + 1)) >> 4)
      push(tmp & 0xFF)
    } else if (placeHolders === 1) {
      tmp = (decode(b64.charAt(i)) << 10) | (decode(b64.charAt(i + 1)) << 4) | (decode(b64.charAt(i + 2)) >> 2)
      push((tmp >> 8) & 0xFF)
      push(tmp & 0xFF)
    }

    return arr
  }

  function uint8ToBase64 (uint8) {
    var i
    var extraBytes = uint8.length % 3 // if we have 1 byte left, pad 2 bytes
    var output = ''
    var temp, length

    function encode (num) {
      return lookup.charAt(num)
    }

    function tripletToBase64 (num) {
      return encode(num >> 18 & 0x3F) + encode(num >> 12 & 0x3F) + encode(num >> 6 & 0x3F) + encode(num & 0x3F)
    }

    // go through the array every three bytes, we'll deal with trailing stuff later
    for (i = 0, length = uint8.length - extraBytes; i < length; i += 3) {
      temp = (uint8[i] << 16) + (uint8[i + 1] << 8) + (uint8[i + 2])
      output += tripletToBase64(temp)
    }

    // pad the end with zeros, but make sure to not forget the extra bytes
    switch (extraBytes) {
      case 1:
        temp = uint8[uint8.length - 1]
        output += encode(temp >> 2)
        output += encode((temp << 4) & 0x3F)
        output += '=='
        break
      case 2:
        temp = (uint8[uint8.length - 2] << 8) + (uint8[uint8.length - 1])
        output += encode(temp >> 10)
        output += encode((temp >> 4) & 0x3F)
        output += encode((temp << 2) & 0x3F)
        output += '='
        break
      default:
        break
    }

    return output
  }

  exports.toByteArray = b64ToByteArray
  exports.fromByteArray = uint8ToBase64
}(typeof exports === 'undefined' ? (this.base64js = {}) : exports))

},{}],21:[function(require,module,exports){
exports.read = function (buffer, offset, isLE, mLen, nBytes) {
  var e, m
  var eLen = nBytes * 8 - mLen - 1
  var eMax = (1 << eLen) - 1
  var eBias = eMax >> 1
  var nBits = -7
  var i = isLE ? (nBytes - 1) : 0
  var d = isLE ? -1 : 1
  var s = buffer[offset + i]

  i += d

  e = s & ((1 << (-nBits)) - 1)
  s >>= (-nBits)
  nBits += eLen
  for (; nBits > 0; e = e * 256 + buffer[offset + i], i += d, nBits -= 8) {}

  m = e & ((1 << (-nBits)) - 1)
  e >>= (-nBits)
  nBits += mLen
  for (; nBits > 0; m = m * 256 + buffer[offset + i], i += d, nBits -= 8) {}

  if (e === 0) {
    e = 1 - eBias
  } else if (e === eMax) {
    return m ? NaN : ((s ? -1 : 1) * Infinity)
  } else {
    m = m + Math.pow(2, mLen)
    e = e - eBias
  }
  return (s ? -1 : 1) * m * Math.pow(2, e - mLen)
}

exports.write = function (buffer, value, offset, isLE, mLen, nBytes) {
  var e, m, c
  var eLen = nBytes * 8 - mLen - 1
  var eMax = (1 << eLen) - 1
  var eBias = eMax >> 1
  var rt = (mLen === 23 ? Math.pow(2, -24) - Math.pow(2, -77) : 0)
  var i = isLE ? 0 : (nBytes - 1)
  var d = isLE ? 1 : -1
  var s = value < 0 || (value === 0 && 1 / value < 0) ? 1 : 0

  value = Math.abs(value)

  if (isNaN(value) || value === Infinity) {
    m = isNaN(value) ? 1 : 0
    e = eMax
  } else {
    e = Math.floor(Math.log(value) / Math.LN2)
    if (value * (c = Math.pow(2, -e)) < 1) {
      e--
      c *= 2
    }
    if (e + eBias >= 1) {
      value += rt / c
    } else {
      value += rt * Math.pow(2, 1 - eBias)
    }
    if (value * c >= 2) {
      e++
      c /= 2
    }

    if (e + eBias >= eMax) {
      m = 0
      e = eMax
    } else if (e + eBias >= 1) {
      m = (value * c - 1) * Math.pow(2, mLen)
      e = e + eBias
    } else {
      m = value * Math.pow(2, eBias - 1) * Math.pow(2, mLen)
      e = 0
    }
  }

  for (; mLen >= 8; buffer[offset + i] = m & 0xff, i += d, m /= 256, mLen -= 8) {}

  e = (e << mLen) | m
  eLen += mLen
  for (; eLen > 0; buffer[offset + i] = e & 0xff, i += d, e /= 256, eLen -= 8) {}

  buffer[offset + i - d] |= s * 128
}

},{}],22:[function(require,module,exports){
var toString = {}.toString;

module.exports = Array.isArray || function (arr) {
  return toString.call(arr) == '[object Array]';
};

},{}],23:[function(require,module,exports){
// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

function EventEmitter() {
  this._events = this._events || {};
  this._maxListeners = this._maxListeners || undefined;
}
module.exports = EventEmitter;

// Backwards-compat with node 0.10.x
EventEmitter.EventEmitter = EventEmitter;

EventEmitter.prototype._events = undefined;
EventEmitter.prototype._maxListeners = undefined;

// By default EventEmitters will print a warning if more than 10 listeners are
// added to it. This is a useful default which helps finding memory leaks.
EventEmitter.defaultMaxListeners = 10;

// Obviously not all Emitters should be limited to 10. This function allows
// that to be increased. Set to zero for unlimited.
EventEmitter.prototype.setMaxListeners = function(n) {
  if (!isNumber(n) || n < 0 || isNaN(n))
    throw TypeError('n must be a positive number');
  this._maxListeners = n;
  return this;
};

EventEmitter.prototype.emit = function(type) {
  var er, handler, len, args, i, listeners;

  if (!this._events)
    this._events = {};

  // If there is no 'error' event listener then throw.
  if (type === 'error') {
    if (!this._events.error ||
        (isObject(this._events.error) && !this._events.error.length)) {
      er = arguments[1];
      if (er instanceof Error) {
        throw er; // Unhandled 'error' event
      }
      throw TypeError('Uncaught, unspecified "error" event.');
    }
  }

  handler = this._events[type];

  if (isUndefined(handler))
    return false;

  if (isFunction(handler)) {
    switch (arguments.length) {
      // fast cases
      case 1:
        handler.call(this);
        break;
      case 2:
        handler.call(this, arguments[1]);
        break;
      case 3:
        handler.call(this, arguments[1], arguments[2]);
        break;
      // slower
      default:
        args = Array.prototype.slice.call(arguments, 1);
        handler.apply(this, args);
    }
  } else if (isObject(handler)) {
    args = Array.prototype.slice.call(arguments, 1);
    listeners = handler.slice();
    len = listeners.length;
    for (i = 0; i < len; i++)
      listeners[i].apply(this, args);
  }

  return true;
};

EventEmitter.prototype.addListener = function(type, listener) {
  var m;

  if (!isFunction(listener))
    throw TypeError('listener must be a function');

  if (!this._events)
    this._events = {};

  // To avoid recursion in the case that type === "newListener"! Before
  // adding it to the listeners, first emit "newListener".
  if (this._events.newListener)
    this.emit('newListener', type,
              isFunction(listener.listener) ?
              listener.listener : listener);

  if (!this._events[type])
    // Optimize the case of one listener. Don't need the extra array object.
    this._events[type] = listener;
  else if (isObject(this._events[type]))
    // If we've already got an array, just append.
    this._events[type].push(listener);
  else
    // Adding the second element, need to change to array.
    this._events[type] = [this._events[type], listener];

  // Check for listener leak
  if (isObject(this._events[type]) && !this._events[type].warned) {
    if (!isUndefined(this._maxListeners)) {
      m = this._maxListeners;
    } else {
      m = EventEmitter.defaultMaxListeners;
    }

    if (m && m > 0 && this._events[type].length > m) {
      this._events[type].warned = true;
      console.error('(node) warning: possible EventEmitter memory ' +
                    'leak detected. %d listeners added. ' +
                    'Use emitter.setMaxListeners() to increase limit.',
                    this._events[type].length);
      if (typeof console.trace === 'function') {
        // not supported in IE 10
        console.trace();
      }
    }
  }

  return this;
};

EventEmitter.prototype.on = EventEmitter.prototype.addListener;

EventEmitter.prototype.once = function(type, listener) {
  if (!isFunction(listener))
    throw TypeError('listener must be a function');

  var fired = false;

  function g() {
    this.removeListener(type, g);

    if (!fired) {
      fired = true;
      listener.apply(this, arguments);
    }
  }

  g.listener = listener;
  this.on(type, g);

  return this;
};

// emits a 'removeListener' event iff the listener was removed
EventEmitter.prototype.removeListener = function(type, listener) {
  var list, position, length, i;

  if (!isFunction(listener))
    throw TypeError('listener must be a function');

  if (!this._events || !this._events[type])
    return this;

  list = this._events[type];
  length = list.length;
  position = -1;

  if (list === listener ||
      (isFunction(list.listener) && list.listener === listener)) {
    delete this._events[type];
    if (this._events.removeListener)
      this.emit('removeListener', type, listener);

  } else if (isObject(list)) {
    for (i = length; i-- > 0;) {
      if (list[i] === listener ||
          (list[i].listener && list[i].listener === listener)) {
        position = i;
        break;
      }
    }

    if (position < 0)
      return this;

    if (list.length === 1) {
      list.length = 0;
      delete this._events[type];
    } else {
      list.splice(position, 1);
    }

    if (this._events.removeListener)
      this.emit('removeListener', type, listener);
  }

  return this;
};

EventEmitter.prototype.removeAllListeners = function(type) {
  var key, listeners;

  if (!this._events)
    return this;

  // not listening for removeListener, no need to emit
  if (!this._events.removeListener) {
    if (arguments.length === 0)
      this._events = {};
    else if (this._events[type])
      delete this._events[type];
    return this;
  }

  // emit removeListener for all listeners on all events
  if (arguments.length === 0) {
    for (key in this._events) {
      if (key === 'removeListener') continue;
      this.removeAllListeners(key);
    }
    this.removeAllListeners('removeListener');
    this._events = {};
    return this;
  }

  listeners = this._events[type];

  if (isFunction(listeners)) {
    this.removeListener(type, listeners);
  } else if (listeners) {
    // LIFO order
    while (listeners.length)
      this.removeListener(type, listeners[listeners.length - 1]);
  }
  delete this._events[type];

  return this;
};

EventEmitter.prototype.listeners = function(type) {
  var ret;
  if (!this._events || !this._events[type])
    ret = [];
  else if (isFunction(this._events[type]))
    ret = [this._events[type]];
  else
    ret = this._events[type].slice();
  return ret;
};

EventEmitter.prototype.listenerCount = function(type) {
  if (this._events) {
    var evlistener = this._events[type];

    if (isFunction(evlistener))
      return 1;
    else if (evlistener)
      return evlistener.length;
  }
  return 0;
};

EventEmitter.listenerCount = function(emitter, type) {
  return emitter.listenerCount(type);
};

function isFunction(arg) {
  return typeof arg === 'function';
}

function isNumber(arg) {
  return typeof arg === 'number';
}

function isObject(arg) {
  return typeof arg === 'object' && arg !== null;
}

function isUndefined(arg) {
  return arg === void 0;
}

},{}],24:[function(require,module,exports){
// shim for using process in browser

var process = module.exports = {};
var queue = [];
var draining = false;
var currentQueue;
var queueIndex = -1;

function cleanUpNextTick() {
    draining = false;
    if (currentQueue.length) {
        queue = currentQueue.concat(queue);
    } else {
        queueIndex = -1;
    }
    if (queue.length) {
        drainQueue();
    }
}

function drainQueue() {
    if (draining) {
        return;
    }
    var timeout = setTimeout(cleanUpNextTick);
    draining = true;

    var len = queue.length;
    while(len) {
        currentQueue = queue;
        queue = [];
        while (++queueIndex < len) {
            if (currentQueue) {
                currentQueue[queueIndex].run();
            }
        }
        queueIndex = -1;
        len = queue.length;
    }
    currentQueue = null;
    draining = false;
    clearTimeout(timeout);
}

process.nextTick = function (fun) {
    var args = new Array(arguments.length - 1);
    if (arguments.length > 1) {
        for (var i = 1; i < arguments.length; i++) {
            args[i - 1] = arguments[i];
        }
    }
    queue.push(new Item(fun, args));
    if (queue.length === 1 && !draining) {
        setTimeout(drainQueue, 0);
    }
};

// v8 likes predictible objects
function Item(fun, array) {
    this.fun = fun;
    this.array = array;
}
Item.prototype.run = function () {
    this.fun.apply(null, this.array);
};
process.title = 'browser';
process.browser = true;
process.env = {};
process.argv = [];
process.version = ''; // empty string to avoid regexp issues
process.versions = {};

function noop() {}

process.on = noop;
process.addListener = noop;
process.once = noop;
process.off = noop;
process.removeListener = noop;
process.removeAllListeners = noop;
process.emit = noop;

process.binding = function (name) {
    throw new Error('process.binding is not supported');
};

process.cwd = function () { return '/' };
process.chdir = function (dir) {
    throw new Error('process.chdir is not supported');
};
process.umask = function() { return 0; };

},{}],25:[function(require,module,exports){
(function (global){
/*! https://mths.be/punycode v1.4.0 by @mathias */
;(function(root) {

	/** Detect free variables */
	var freeExports = typeof exports == 'object' && exports &&
		!exports.nodeType && exports;
	var freeModule = typeof module == 'object' && module &&
		!module.nodeType && module;
	var freeGlobal = typeof global == 'object' && global;
	if (
		freeGlobal.global === freeGlobal ||
		freeGlobal.window === freeGlobal ||
		freeGlobal.self === freeGlobal
	) {
		root = freeGlobal;
	}

	/**
	 * The `punycode` object.
	 * @name punycode
	 * @type Object
	 */
	var punycode,

	/** Highest positive signed 32-bit float value */
	maxInt = 2147483647, // aka. 0x7FFFFFFF or 2^31-1

	/** Bootstring parameters */
	base = 36,
	tMin = 1,
	tMax = 26,
	skew = 38,
	damp = 700,
	initialBias = 72,
	initialN = 128, // 0x80
	delimiter = '-', // '\x2D'

	/** Regular expressions */
	regexPunycode = /^xn--/,
	regexNonASCII = /[^\x20-\x7E]/, // unprintable ASCII chars + non-ASCII chars
	regexSeparators = /[\x2E\u3002\uFF0E\uFF61]/g, // RFC 3490 separators

	/** Error messages */
	errors = {
		'overflow': 'Overflow: input needs wider integers to process',
		'not-basic': 'Illegal input >= 0x80 (not a basic code point)',
		'invalid-input': 'Invalid input'
	},

	/** Convenience shortcuts */
	baseMinusTMin = base - tMin,
	floor = Math.floor,
	stringFromCharCode = String.fromCharCode,

	/** Temporary variable */
	key;

	/*--------------------------------------------------------------------------*/

	/**
	 * A generic error utility function.
	 * @private
	 * @param {String} type The error type.
	 * @returns {Error} Throws a `RangeError` with the applicable error message.
	 */
	function error(type) {
		throw new RangeError(errors[type]);
	}

	/**
	 * A generic `Array#map` utility function.
	 * @private
	 * @param {Array} array The array to iterate over.
	 * @param {Function} callback The function that gets called for every array
	 * item.
	 * @returns {Array} A new array of values returned by the callback function.
	 */
	function map(array, fn) {
		var length = array.length;
		var result = [];
		while (length--) {
			result[length] = fn(array[length]);
		}
		return result;
	}

	/**
	 * A simple `Array#map`-like wrapper to work with domain name strings or email
	 * addresses.
	 * @private
	 * @param {String} domain The domain name or email address.
	 * @param {Function} callback The function that gets called for every
	 * character.
	 * @returns {Array} A new string of characters returned by the callback
	 * function.
	 */
	function mapDomain(string, fn) {
		var parts = string.split('@');
		var result = '';
		if (parts.length > 1) {
			// In email addresses, only the domain name should be punycoded. Leave
			// the local part (i.e. everything up to `@`) intact.
			result = parts[0] + '@';
			string = parts[1];
		}
		// Avoid `split(regex)` for IE8 compatibility. See #17.
		string = string.replace(regexSeparators, '\x2E');
		var labels = string.split('.');
		var encoded = map(labels, fn).join('.');
		return result + encoded;
	}

	/**
	 * Creates an array containing the numeric code points of each Unicode
	 * character in the string. While JavaScript uses UCS-2 internally,
	 * this function will convert a pair of surrogate halves (each of which
	 * UCS-2 exposes as separate characters) into a single code point,
	 * matching UTF-16.
	 * @see `punycode.ucs2.encode`
	 * @see <https://mathiasbynens.be/notes/javascript-encoding>
	 * @memberOf punycode.ucs2
	 * @name decode
	 * @param {String} string The Unicode input string (UCS-2).
	 * @returns {Array} The new array of code points.
	 */
	function ucs2decode(string) {
		var output = [],
		    counter = 0,
		    length = string.length,
		    value,
		    extra;
		while (counter < length) {
			value = string.charCodeAt(counter++);
			if (value >= 0xD800 && value <= 0xDBFF && counter < length) {
				// high surrogate, and there is a next character
				extra = string.charCodeAt(counter++);
				if ((extra & 0xFC00) == 0xDC00) { // low surrogate
					output.push(((value & 0x3FF) << 10) + (extra & 0x3FF) + 0x10000);
				} else {
					// unmatched surrogate; only append this code unit, in case the next
					// code unit is the high surrogate of a surrogate pair
					output.push(value);
					counter--;
				}
			} else {
				output.push(value);
			}
		}
		return output;
	}

	/**
	 * Creates a string based on an array of numeric code points.
	 * @see `punycode.ucs2.decode`
	 * @memberOf punycode.ucs2
	 * @name encode
	 * @param {Array} codePoints The array of numeric code points.
	 * @returns {String} The new Unicode string (UCS-2).
	 */
	function ucs2encode(array) {
		return map(array, function(value) {
			var output = '';
			if (value > 0xFFFF) {
				value -= 0x10000;
				output += stringFromCharCode(value >>> 10 & 0x3FF | 0xD800);
				value = 0xDC00 | value & 0x3FF;
			}
			output += stringFromCharCode(value);
			return output;
		}).join('');
	}

	/**
	 * Converts a basic code point into a digit/integer.
	 * @see `digitToBasic()`
	 * @private
	 * @param {Number} codePoint The basic numeric code point value.
	 * @returns {Number} The numeric value of a basic code point (for use in
	 * representing integers) in the range `0` to `base - 1`, or `base` if
	 * the code point does not represent a value.
	 */
	function basicToDigit(codePoint) {
		if (codePoint - 48 < 10) {
			return codePoint - 22;
		}
		if (codePoint - 65 < 26) {
			return codePoint - 65;
		}
		if (codePoint - 97 < 26) {
			return codePoint - 97;
		}
		return base;
	}

	/**
	 * Converts a digit/integer into a basic code point.
	 * @see `basicToDigit()`
	 * @private
	 * @param {Number} digit The numeric value of a basic code point.
	 * @returns {Number} The basic code point whose value (when used for
	 * representing integers) is `digit`, which needs to be in the range
	 * `0` to `base - 1`. If `flag` is non-zero, the uppercase form is
	 * used; else, the lowercase form is used. The behavior is undefined
	 * if `flag` is non-zero and `digit` has no uppercase form.
	 */
	function digitToBasic(digit, flag) {
		//  0..25 map to ASCII a..z or A..Z
		// 26..35 map to ASCII 0..9
		return digit + 22 + 75 * (digit < 26) - ((flag != 0) << 5);
	}

	/**
	 * Bias adaptation function as per section 3.4 of RFC 3492.
	 * https://tools.ietf.org/html/rfc3492#section-3.4
	 * @private
	 */
	function adapt(delta, numPoints, firstTime) {
		var k = 0;
		delta = firstTime ? floor(delta / damp) : delta >> 1;
		delta += floor(delta / numPoints);
		for (/* no initialization */; delta > baseMinusTMin * tMax >> 1; k += base) {
			delta = floor(delta / baseMinusTMin);
		}
		return floor(k + (baseMinusTMin + 1) * delta / (delta + skew));
	}

	/**
	 * Converts a Punycode string of ASCII-only symbols to a string of Unicode
	 * symbols.
	 * @memberOf punycode
	 * @param {String} input The Punycode string of ASCII-only symbols.
	 * @returns {String} The resulting string of Unicode symbols.
	 */
	function decode(input) {
		// Don't use UCS-2
		var output = [],
		    inputLength = input.length,
		    out,
		    i = 0,
		    n = initialN,
		    bias = initialBias,
		    basic,
		    j,
		    index,
		    oldi,
		    w,
		    k,
		    digit,
		    t,
		    /** Cached calculation results */
		    baseMinusT;

		// Handle the basic code points: let `basic` be the number of input code
		// points before the last delimiter, or `0` if there is none, then copy
		// the first basic code points to the output.

		basic = input.lastIndexOf(delimiter);
		if (basic < 0) {
			basic = 0;
		}

		for (j = 0; j < basic; ++j) {
			// if it's not a basic code point
			if (input.charCodeAt(j) >= 0x80) {
				error('not-basic');
			}
			output.push(input.charCodeAt(j));
		}

		// Main decoding loop: start just after the last delimiter if any basic code
		// points were copied; start at the beginning otherwise.

		for (index = basic > 0 ? basic + 1 : 0; index < inputLength; /* no final expression */) {

			// `index` is the index of the next character to be consumed.
			// Decode a generalized variable-length integer into `delta`,
			// which gets added to `i`. The overflow checking is easier
			// if we increase `i` as we go, then subtract off its starting
			// value at the end to obtain `delta`.
			for (oldi = i, w = 1, k = base; /* no condition */; k += base) {

				if (index >= inputLength) {
					error('invalid-input');
				}

				digit = basicToDigit(input.charCodeAt(index++));

				if (digit >= base || digit > floor((maxInt - i) / w)) {
					error('overflow');
				}

				i += digit * w;
				t = k <= bias ? tMin : (k >= bias + tMax ? tMax : k - bias);

				if (digit < t) {
					break;
				}

				baseMinusT = base - t;
				if (w > floor(maxInt / baseMinusT)) {
					error('overflow');
				}

				w *= baseMinusT;

			}

			out = output.length + 1;
			bias = adapt(i - oldi, out, oldi == 0);

			// `i` was supposed to wrap around from `out` to `0`,
			// incrementing `n` each time, so we'll fix that now:
			if (floor(i / out) > maxInt - n) {
				error('overflow');
			}

			n += floor(i / out);
			i %= out;

			// Insert `n` at position `i` of the output
			output.splice(i++, 0, n);

		}

		return ucs2encode(output);
	}

	/**
	 * Converts a string of Unicode symbols (e.g. a domain name label) to a
	 * Punycode string of ASCII-only symbols.
	 * @memberOf punycode
	 * @param {String} input The string of Unicode symbols.
	 * @returns {String} The resulting Punycode string of ASCII-only symbols.
	 */
	function encode(input) {
		var n,
		    delta,
		    handledCPCount,
		    basicLength,
		    bias,
		    j,
		    m,
		    q,
		    k,
		    t,
		    currentValue,
		    output = [],
		    /** `inputLength` will hold the number of code points in `input`. */
		    inputLength,
		    /** Cached calculation results */
		    handledCPCountPlusOne,
		    baseMinusT,
		    qMinusT;

		// Convert the input in UCS-2 to Unicode
		input = ucs2decode(input);

		// Cache the length
		inputLength = input.length;

		// Initialize the state
		n = initialN;
		delta = 0;
		bias = initialBias;

		// Handle the basic code points
		for (j = 0; j < inputLength; ++j) {
			currentValue = input[j];
			if (currentValue < 0x80) {
				output.push(stringFromCharCode(currentValue));
			}
		}

		handledCPCount = basicLength = output.length;

		// `handledCPCount` is the number of code points that have been handled;
		// `basicLength` is the number of basic code points.

		// Finish the basic string - if it is not empty - with a delimiter
		if (basicLength) {
			output.push(delimiter);
		}

		// Main encoding loop:
		while (handledCPCount < inputLength) {

			// All non-basic code points < n have been handled already. Find the next
			// larger one:
			for (m = maxInt, j = 0; j < inputLength; ++j) {
				currentValue = input[j];
				if (currentValue >= n && currentValue < m) {
					m = currentValue;
				}
			}

			// Increase `delta` enough to advance the decoder's <n,i> state to <m,0>,
			// but guard against overflow
			handledCPCountPlusOne = handledCPCount + 1;
			if (m - n > floor((maxInt - delta) / handledCPCountPlusOne)) {
				error('overflow');
			}

			delta += (m - n) * handledCPCountPlusOne;
			n = m;

			for (j = 0; j < inputLength; ++j) {
				currentValue = input[j];

				if (currentValue < n && ++delta > maxInt) {
					error('overflow');
				}

				if (currentValue == n) {
					// Represent delta as a generalized variable-length integer
					for (q = delta, k = base; /* no condition */; k += base) {
						t = k <= bias ? tMin : (k >= bias + tMax ? tMax : k - bias);
						if (q < t) {
							break;
						}
						qMinusT = q - t;
						baseMinusT = base - t;
						output.push(
							stringFromCharCode(digitToBasic(t + qMinusT % baseMinusT, 0))
						);
						q = floor(qMinusT / baseMinusT);
					}

					output.push(stringFromCharCode(digitToBasic(q, 0)));
					bias = adapt(delta, handledCPCountPlusOne, handledCPCount == basicLength);
					delta = 0;
					++handledCPCount;
				}
			}

			++delta;
			++n;

		}
		return output.join('');
	}

	/**
	 * Converts a Punycode string representing a domain name or an email address
	 * to Unicode. Only the Punycoded parts of the input will be converted, i.e.
	 * it doesn't matter if you call it on a string that has already been
	 * converted to Unicode.
	 * @memberOf punycode
	 * @param {String} input The Punycoded domain name or email address to
	 * convert to Unicode.
	 * @returns {String} The Unicode representation of the given Punycode
	 * string.
	 */
	function toUnicode(input) {
		return mapDomain(input, function(string) {
			return regexPunycode.test(string)
				? decode(string.slice(4).toLowerCase())
				: string;
		});
	}

	/**
	 * Converts a Unicode string representing a domain name or an email address to
	 * Punycode. Only the non-ASCII parts of the domain name will be converted,
	 * i.e. it doesn't matter if you call it with a domain that's already in
	 * ASCII.
	 * @memberOf punycode
	 * @param {String} input The domain name or email address to convert, as a
	 * Unicode string.
	 * @returns {String} The Punycode representation of the given domain name or
	 * email address.
	 */
	function toASCII(input) {
		return mapDomain(input, function(string) {
			return regexNonASCII.test(string)
				? 'xn--' + encode(string)
				: string;
		});
	}

	/*--------------------------------------------------------------------------*/

	/** Define the public API */
	punycode = {
		/**
		 * A string representing the current Punycode.js version number.
		 * @memberOf punycode
		 * @type String
		 */
		'version': '1.3.2',
		/**
		 * An object of methods to convert from JavaScript's internal character
		 * representation (UCS-2) to Unicode code points, and back.
		 * @see <https://mathiasbynens.be/notes/javascript-encoding>
		 * @memberOf punycode
		 * @type Object
		 */
		'ucs2': {
			'decode': ucs2decode,
			'encode': ucs2encode
		},
		'decode': decode,
		'encode': encode,
		'toASCII': toASCII,
		'toUnicode': toUnicode
	};

	/** Expose `punycode` */
	// Some AMD build optimizers, like r.js, check for specific condition patterns
	// like the following:
	if (
		typeof define == 'function' &&
		typeof define.amd == 'object' &&
		define.amd
	) {
		define('punycode', function() {
			return punycode;
		});
	} else if (freeExports && freeModule) {
		if (module.exports == freeExports) {
			// in Node.js, io.js, or RingoJS v0.8.0+
			freeModule.exports = punycode;
		} else {
			// in Narwhal or RingoJS v0.7.0-
			for (key in punycode) {
				punycode.hasOwnProperty(key) && (freeExports[key] = punycode[key]);
			}
		}
	} else {
		// in Rhino or a web browser
		root.punycode = punycode;
	}

}(this));

}).call(this,typeof global !== "undefined" ? global : typeof self !== "undefined" ? self : typeof window !== "undefined" ? window : {})
},{}],26:[function(require,module,exports){
// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';

// If obj.hasOwnProperty has been overridden, then calling
// obj.hasOwnProperty(prop) will break.
// See: https://github.com/joyent/node/issues/1707
function hasOwnProperty(obj, prop) {
  return Object.prototype.hasOwnProperty.call(obj, prop);
}

module.exports = function(qs, sep, eq, options) {
  sep = sep || '&';
  eq = eq || '=';
  var obj = {};

  if (typeof qs !== 'string' || qs.length === 0) {
    return obj;
  }

  var regexp = /\+/g;
  qs = qs.split(sep);

  var maxKeys = 1000;
  if (options && typeof options.maxKeys === 'number') {
    maxKeys = options.maxKeys;
  }

  var len = qs.length;
  // maxKeys <= 0 means that we should not limit keys count
  if (maxKeys > 0 && len > maxKeys) {
    len = maxKeys;
  }

  for (var i = 0; i < len; ++i) {
    var x = qs[i].replace(regexp, '%20'),
        idx = x.indexOf(eq),
        kstr, vstr, k, v;

    if (idx >= 0) {
      kstr = x.substr(0, idx);
      vstr = x.substr(idx + 1);
    } else {
      kstr = x;
      vstr = '';
    }

    k = decodeURIComponent(kstr);
    v = decodeURIComponent(vstr);

    if (!hasOwnProperty(obj, k)) {
      obj[k] = v;
    } else if (isArray(obj[k])) {
      obj[k].push(v);
    } else {
      obj[k] = [obj[k], v];
    }
  }

  return obj;
};

var isArray = Array.isArray || function (xs) {
  return Object.prototype.toString.call(xs) === '[object Array]';
};

},{}],27:[function(require,module,exports){
// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';

var stringifyPrimitive = function(v) {
  switch (typeof v) {
    case 'string':
      return v;

    case 'boolean':
      return v ? 'true' : 'false';

    case 'number':
      return isFinite(v) ? v : '';

    default:
      return '';
  }
};

module.exports = function(obj, sep, eq, name) {
  sep = sep || '&';
  eq = eq || '=';
  if (obj === null) {
    obj = undefined;
  }

  if (typeof obj === 'object') {
    return map(objectKeys(obj), function(k) {
      var ks = encodeURIComponent(stringifyPrimitive(k)) + eq;
      if (isArray(obj[k])) {
        return map(obj[k], function(v) {
          return ks + encodeURIComponent(stringifyPrimitive(v));
        }).join(sep);
      } else {
        return ks + encodeURIComponent(stringifyPrimitive(obj[k]));
      }
    }).join(sep);

  }

  if (!name) return '';
  return encodeURIComponent(stringifyPrimitive(name)) + eq +
         encodeURIComponent(stringifyPrimitive(obj));
};

var isArray = Array.isArray || function (xs) {
  return Object.prototype.toString.call(xs) === '[object Array]';
};

function map (xs, f) {
  if (xs.map) return xs.map(f);
  var res = [];
  for (var i = 0; i < xs.length; i++) {
    res.push(f(xs[i], i));
  }
  return res;
}

var objectKeys = Object.keys || function (obj) {
  var res = [];
  for (var key in obj) {
    if (Object.prototype.hasOwnProperty.call(obj, key)) res.push(key);
  }
  return res;
};

},{}],28:[function(require,module,exports){
'use strict';

exports.decode = exports.parse = require('./decode');
exports.encode = exports.stringify = require('./encode');

},{"./decode":26,"./encode":27}],29:[function(require,module,exports){
// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';

var punycode = require('punycode');
var util = require('./util');

exports.parse = urlParse;
exports.resolve = urlResolve;
exports.resolveObject = urlResolveObject;
exports.format = urlFormat;

exports.Url = Url;

function Url() {
  this.protocol = null;
  this.slashes = null;
  this.auth = null;
  this.host = null;
  this.port = null;
  this.hostname = null;
  this.hash = null;
  this.search = null;
  this.query = null;
  this.pathname = null;
  this.path = null;
  this.href = null;
}

// Reference: RFC 3986, RFC 1808, RFC 2396

// define these here so at least they only have to be
// compiled once on the first module load.
var protocolPattern = /^([a-z0-9.+-]+:)/i,
    portPattern = /:[0-9]*$/,

    // Special case for a simple path URL
    simplePathPattern = /^(\/\/?(?!\/)[^\?\s]*)(\?[^\s]*)?$/,

    // RFC 2396: characters reserved for delimiting URLs.
    // We actually just auto-escape these.
    delims = ['<', '>', '"', '`', ' ', '\r', '\n', '\t'],

    // RFC 2396: characters not allowed for various reasons.
    unwise = ['{', '}', '|', '\\', '^', '`'].concat(delims),

    // Allowed by RFCs, but cause of XSS attacks.  Always escape these.
    autoEscape = ['\''].concat(unwise),
    // Characters that are never ever allowed in a hostname.
    // Note that any invalid chars are also handled, but these
    // are the ones that are *expected* to be seen, so we fast-path
    // them.
    nonHostChars = ['%', '/', '?', ';', '#'].concat(autoEscape),
    hostEndingChars = ['/', '?', '#'],
    hostnameMaxLen = 255,
    hostnamePartPattern = /^[+a-z0-9A-Z_-]{0,63}$/,
    hostnamePartStart = /^([+a-z0-9A-Z_-]{0,63})(.*)$/,
    // protocols that can allow "unsafe" and "unwise" chars.
    unsafeProtocol = {
      'javascript': true,
      'javascript:': true
    },
    // protocols that never have a hostname.
    hostlessProtocol = {
      'javascript': true,
      'javascript:': true
    },
    // protocols that always contain a // bit.
    slashedProtocol = {
      'http': true,
      'https': true,
      'ftp': true,
      'gopher': true,
      'file': true,
      'http:': true,
      'https:': true,
      'ftp:': true,
      'gopher:': true,
      'file:': true
    },
    querystring = require('querystring');

function urlParse(url, parseQueryString, slashesDenoteHost) {
  if (url && util.isObject(url) && url instanceof Url) return url;

  var u = new Url;
  u.parse(url, parseQueryString, slashesDenoteHost);
  return u;
}

Url.prototype.parse = function(url, parseQueryString, slashesDenoteHost) {
  if (!util.isString(url)) {
    throw new TypeError("Parameter 'url' must be a string, not " + typeof url);
  }

  // Copy chrome, IE, opera backslash-handling behavior.
  // Back slashes before the query string get converted to forward slashes
  // See: https://code.google.com/p/chromium/issues/detail?id=25916
  var queryIndex = url.indexOf('?'),
      splitter =
          (queryIndex !== -1 && queryIndex < url.indexOf('#')) ? '?' : '#',
      uSplit = url.split(splitter),
      slashRegex = /\\/g;
  uSplit[0] = uSplit[0].replace(slashRegex, '/');
  url = uSplit.join(splitter);

  var rest = url;

  // trim before proceeding.
  // This is to support parse stuff like "  http://foo.com  \n"
  rest = rest.trim();

  if (!slashesDenoteHost && url.split('#').length === 1) {
    // Try fast path regexp
    var simplePath = simplePathPattern.exec(rest);
    if (simplePath) {
      this.path = rest;
      this.href = rest;
      this.pathname = simplePath[1];
      if (simplePath[2]) {
        this.search = simplePath[2];
        if (parseQueryString) {
          this.query = querystring.parse(this.search.substr(1));
        } else {
          this.query = this.search.substr(1);
        }
      } else if (parseQueryString) {
        this.search = '';
        this.query = {};
      }
      return this;
    }
  }

  var proto = protocolPattern.exec(rest);
  if (proto) {
    proto = proto[0];
    var lowerProto = proto.toLowerCase();
    this.protocol = lowerProto;
    rest = rest.substr(proto.length);
  }

  // figure out if it's got a host
  // user@server is *always* interpreted as a hostname, and url
  // resolution will treat //foo/bar as host=foo,path=bar because that's
  // how the browser resolves relative URLs.
  if (slashesDenoteHost || proto || rest.match(/^\/\/[^@\/]+@[^@\/]+/)) {
    var slashes = rest.substr(0, 2) === '//';
    if (slashes && !(proto && hostlessProtocol[proto])) {
      rest = rest.substr(2);
      this.slashes = true;
    }
  }

  if (!hostlessProtocol[proto] &&
      (slashes || (proto && !slashedProtocol[proto]))) {

    // there's a hostname.
    // the first instance of /, ?, ;, or # ends the host.
    //
    // If there is an @ in the hostname, then non-host chars *are* allowed
    // to the left of the last @ sign, unless some host-ending character
    // comes *before* the @-sign.
    // URLs are obnoxious.
    //
    // ex:
    // http://a@b@c/ => user:a@b host:c
    // http://a@b?@c => user:a host:c path:/?@c

    // v0.12 TODO(isaacs): This is not quite how Chrome does things.
    // Review our test case against browsers more comprehensively.

    // find the first instance of any hostEndingChars
    var hostEnd = -1;
    for (var i = 0; i < hostEndingChars.length; i++) {
      var hec = rest.indexOf(hostEndingChars[i]);
      if (hec !== -1 && (hostEnd === -1 || hec < hostEnd))
        hostEnd = hec;
    }

    // at this point, either we have an explicit point where the
    // auth portion cannot go past, or the last @ char is the decider.
    var auth, atSign;
    if (hostEnd === -1) {
      // atSign can be anywhere.
      atSign = rest.lastIndexOf('@');
    } else {
      // atSign must be in auth portion.
      // http://a@b/c@d => host:b auth:a path:/c@d
      atSign = rest.lastIndexOf('@', hostEnd);
    }

    // Now we have a portion which is definitely the auth.
    // Pull that off.
    if (atSign !== -1) {
      auth = rest.slice(0, atSign);
      rest = rest.slice(atSign + 1);
      this.auth = decodeURIComponent(auth);
    }

    // the host is the remaining to the left of the first non-host char
    hostEnd = -1;
    for (var i = 0; i < nonHostChars.length; i++) {
      var hec = rest.indexOf(nonHostChars[i]);
      if (hec !== -1 && (hostEnd === -1 || hec < hostEnd))
        hostEnd = hec;
    }
    // if we still have not hit it, then the entire thing is a host.
    if (hostEnd === -1)
      hostEnd = rest.length;

    this.host = rest.slice(0, hostEnd);
    rest = rest.slice(hostEnd);

    // pull out port.
    this.parseHost();

    // we've indicated that there is a hostname,
    // so even if it's empty, it has to be present.
    this.hostname = this.hostname || '';

    // if hostname begins with [ and ends with ]
    // assume that it's an IPv6 address.
    var ipv6Hostname = this.hostname[0] === '[' &&
        this.hostname[this.hostname.length - 1] === ']';

    // validate a little.
    if (!ipv6Hostname) {
      var hostparts = this.hostname.split(/\./);
      for (var i = 0, l = hostparts.length; i < l; i++) {
        var part = hostparts[i];
        if (!part) continue;
        if (!part.match(hostnamePartPattern)) {
          var newpart = '';
          for (var j = 0, k = part.length; j < k; j++) {
            if (part.charCodeAt(j) > 127) {
              // we replace non-ASCII char with a temporary placeholder
              // we need this to make sure size of hostname is not
              // broken by replacing non-ASCII by nothing
              newpart += 'x';
            } else {
              newpart += part[j];
            }
          }
          // we test again with ASCII char only
          if (!newpart.match(hostnamePartPattern)) {
            var validParts = hostparts.slice(0, i);
            var notHost = hostparts.slice(i + 1);
            var bit = part.match(hostnamePartStart);
            if (bit) {
              validParts.push(bit[1]);
              notHost.unshift(bit[2]);
            }
            if (notHost.length) {
              rest = '/' + notHost.join('.') + rest;
            }
            this.hostname = validParts.join('.');
            break;
          }
        }
      }
    }

    if (this.hostname.length > hostnameMaxLen) {
      this.hostname = '';
    } else {
      // hostnames are always lower case.
      this.hostname = this.hostname.toLowerCase();
    }

    if (!ipv6Hostname) {
      // IDNA Support: Returns a punycoded representation of "domain".
      // It only converts parts of the domain name that
      // have non-ASCII characters, i.e. it doesn't matter if
      // you call it with a domain that already is ASCII-only.
      this.hostname = punycode.toASCII(this.hostname);
    }

    var p = this.port ? ':' + this.port : '';
    var h = this.hostname || '';
    this.host = h + p;
    this.href += this.host;

    // strip [ and ] from the hostname
    // the host field still retains them, though
    if (ipv6Hostname) {
      this.hostname = this.hostname.substr(1, this.hostname.length - 2);
      if (rest[0] !== '/') {
        rest = '/' + rest;
      }
    }
  }

  // now rest is set to the post-host stuff.
  // chop off any delim chars.
  if (!unsafeProtocol[lowerProto]) {

    // First, make 100% sure that any "autoEscape" chars get
    // escaped, even if encodeURIComponent doesn't think they
    // need to be.
    for (var i = 0, l = autoEscape.length; i < l; i++) {
      var ae = autoEscape[i];
      if (rest.indexOf(ae) === -1)
        continue;
      var esc = encodeURIComponent(ae);
      if (esc === ae) {
        esc = escape(ae);
      }
      rest = rest.split(ae).join(esc);
    }
  }


  // chop off from the tail first.
  var hash = rest.indexOf('#');
  if (hash !== -1) {
    // got a fragment string.
    this.hash = rest.substr(hash);
    rest = rest.slice(0, hash);
  }
  var qm = rest.indexOf('?');
  if (qm !== -1) {
    this.search = rest.substr(qm);
    this.query = rest.substr(qm + 1);
    if (parseQueryString) {
      this.query = querystring.parse(this.query);
    }
    rest = rest.slice(0, qm);
  } else if (parseQueryString) {
    // no query string, but parseQueryString still requested
    this.search = '';
    this.query = {};
  }
  if (rest) this.pathname = rest;
  if (slashedProtocol[lowerProto] &&
      this.hostname && !this.pathname) {
    this.pathname = '/';
  }

  //to support http.request
  if (this.pathname || this.search) {
    var p = this.pathname || '';
    var s = this.search || '';
    this.path = p + s;
  }

  // finally, reconstruct the href based on what has been validated.
  this.href = this.format();
  return this;
};

// format a parsed object into a url string
function urlFormat(obj) {
  // ensure it's an object, and not a string url.
  // If it's an obj, this is a no-op.
  // this way, you can call url_format() on strings
  // to clean up potentially wonky urls.
  if (util.isString(obj)) obj = urlParse(obj);
  if (!(obj instanceof Url)) return Url.prototype.format.call(obj);
  return obj.format();
}

Url.prototype.format = function() {
  var auth = this.auth || '';
  if (auth) {
    auth = encodeURIComponent(auth);
    auth = auth.replace(/%3A/i, ':');
    auth += '@';
  }

  var protocol = this.protocol || '',
      pathname = this.pathname || '',
      hash = this.hash || '',
      host = false,
      query = '';

  if (this.host) {
    host = auth + this.host;
  } else if (this.hostname) {
    host = auth + (this.hostname.indexOf(':') === -1 ?
        this.hostname :
        '[' + this.hostname + ']');
    if (this.port) {
      host += ':' + this.port;
    }
  }

  if (this.query &&
      util.isObject(this.query) &&
      Object.keys(this.query).length) {
    query = querystring.stringify(this.query);
  }

  var search = this.search || (query && ('?' + query)) || '';

  if (protocol && protocol.substr(-1) !== ':') protocol += ':';

  // only the slashedProtocols get the //.  Not mailto:, xmpp:, etc.
  // unless they had them to begin with.
  if (this.slashes ||
      (!protocol || slashedProtocol[protocol]) && host !== false) {
    host = '//' + (host || '');
    if (pathname && pathname.charAt(0) !== '/') pathname = '/' + pathname;
  } else if (!host) {
    host = '';
  }

  if (hash && hash.charAt(0) !== '#') hash = '#' + hash;
  if (search && search.charAt(0) !== '?') search = '?' + search;

  pathname = pathname.replace(/[?#]/g, function(match) {
    return encodeURIComponent(match);
  });
  search = search.replace('#', '%23');

  return protocol + host + pathname + search + hash;
};

function urlResolve(source, relative) {
  return urlParse(source, false, true).resolve(relative);
}

Url.prototype.resolve = function(relative) {
  return this.resolveObject(urlParse(relative, false, true)).format();
};

function urlResolveObject(source, relative) {
  if (!source) return relative;
  return urlParse(source, false, true).resolveObject(relative);
}

Url.prototype.resolveObject = function(relative) {
  if (util.isString(relative)) {
    var rel = new Url();
    rel.parse(relative, false, true);
    relative = rel;
  }

  var result = new Url();
  var tkeys = Object.keys(this);
  for (var tk = 0; tk < tkeys.length; tk++) {
    var tkey = tkeys[tk];
    result[tkey] = this[tkey];
  }

  // hash is always overridden, no matter what.
  // even href="" will remove it.
  result.hash = relative.hash;

  // if the relative url is empty, then there's nothing left to do here.
  if (relative.href === '') {
    result.href = result.format();
    return result;
  }

  // hrefs like //foo/bar always cut to the protocol.
  if (relative.slashes && !relative.protocol) {
    // take everything except the protocol from relative
    var rkeys = Object.keys(relative);
    for (var rk = 0; rk < rkeys.length; rk++) {
      var rkey = rkeys[rk];
      if (rkey !== 'protocol')
        result[rkey] = relative[rkey];
    }

    //urlParse appends trailing / to urls like http://www.example.com
    if (slashedProtocol[result.protocol] &&
        result.hostname && !result.pathname) {
      result.path = result.pathname = '/';
    }

    result.href = result.format();
    return result;
  }

  if (relative.protocol && relative.protocol !== result.protocol) {
    // if it's a known url protocol, then changing
    // the protocol does weird things
    // first, if it's not file:, then we MUST have a host,
    // and if there was a path
    // to begin with, then we MUST have a path.
    // if it is file:, then the host is dropped,
    // because that's known to be hostless.
    // anything else is assumed to be absolute.
    if (!slashedProtocol[relative.protocol]) {
      var keys = Object.keys(relative);
      for (var v = 0; v < keys.length; v++) {
        var k = keys[v];
        result[k] = relative[k];
      }
      result.href = result.format();
      return result;
    }

    result.protocol = relative.protocol;
    if (!relative.host && !hostlessProtocol[relative.protocol]) {
      var relPath = (relative.pathname || '').split('/');
      while (relPath.length && !(relative.host = relPath.shift()));
      if (!relative.host) relative.host = '';
      if (!relative.hostname) relative.hostname = '';
      if (relPath[0] !== '') relPath.unshift('');
      if (relPath.length < 2) relPath.unshift('');
      result.pathname = relPath.join('/');
    } else {
      result.pathname = relative.pathname;
    }
    result.search = relative.search;
    result.query = relative.query;
    result.host = relative.host || '';
    result.auth = relative.auth;
    result.hostname = relative.hostname || relative.host;
    result.port = relative.port;
    // to support http.request
    if (result.pathname || result.search) {
      var p = result.pathname || '';
      var s = result.search || '';
      result.path = p + s;
    }
    result.slashes = result.slashes || relative.slashes;
    result.href = result.format();
    return result;
  }

  var isSourceAbs = (result.pathname && result.pathname.charAt(0) === '/'),
      isRelAbs = (
          relative.host ||
          relative.pathname && relative.pathname.charAt(0) === '/'
      ),
      mustEndAbs = (isRelAbs || isSourceAbs ||
                    (result.host && relative.pathname)),
      removeAllDots = mustEndAbs,
      srcPath = result.pathname && result.pathname.split('/') || [],
      relPath = relative.pathname && relative.pathname.split('/') || [],
      psychotic = result.protocol && !slashedProtocol[result.protocol];

  // if the url is a non-slashed url, then relative
  // links like ../.. should be able
  // to crawl up to the hostname, as well.  This is strange.
  // result.protocol has already been set by now.
  // Later on, put the first path part into the host field.
  if (psychotic) {
    result.hostname = '';
    result.port = null;
    if (result.host) {
      if (srcPath[0] === '') srcPath[0] = result.host;
      else srcPath.unshift(result.host);
    }
    result.host = '';
    if (relative.protocol) {
      relative.hostname = null;
      relative.port = null;
      if (relative.host) {
        if (relPath[0] === '') relPath[0] = relative.host;
        else relPath.unshift(relative.host);
      }
      relative.host = null;
    }
    mustEndAbs = mustEndAbs && (relPath[0] === '' || srcPath[0] === '');
  }

  if (isRelAbs) {
    // it's absolute.
    result.host = (relative.host || relative.host === '') ?
                  relative.host : result.host;
    result.hostname = (relative.hostname || relative.hostname === '') ?
                      relative.hostname : result.hostname;
    result.search = relative.search;
    result.query = relative.query;
    srcPath = relPath;
    // fall through to the dot-handling below.
  } else if (relPath.length) {
    // it's relative
    // throw away the existing file, and take the new path instead.
    if (!srcPath) srcPath = [];
    srcPath.pop();
    srcPath = srcPath.concat(relPath);
    result.search = relative.search;
    result.query = relative.query;
  } else if (!util.isNullOrUndefined(relative.search)) {
    // just pull out the search.
    // like href='?foo'.
    // Put this after the other two cases because it simplifies the booleans
    if (psychotic) {
      result.hostname = result.host = srcPath.shift();
      //occationaly the auth can get stuck only in host
      //this especially happens in cases like
      //url.resolveObject('mailto:local1@domain1', 'local2@domain2')
      var authInHost = result.host && result.host.indexOf('@') > 0 ?
                       result.host.split('@') : false;
      if (authInHost) {
        result.auth = authInHost.shift();
        result.host = result.hostname = authInHost.shift();
      }
    }
    result.search = relative.search;
    result.query = relative.query;
    //to support http.request
    if (!util.isNull(result.pathname) || !util.isNull(result.search)) {
      result.path = (result.pathname ? result.pathname : '') +
                    (result.search ? result.search : '');
    }
    result.href = result.format();
    return result;
  }

  if (!srcPath.length) {
    // no path at all.  easy.
    // we've already handled the other stuff above.
    result.pathname = null;
    //to support http.request
    if (result.search) {
      result.path = '/' + result.search;
    } else {
      result.path = null;
    }
    result.href = result.format();
    return result;
  }

  // if a url ENDs in . or .., then it must get a trailing slash.
  // however, if it ends in anything else non-slashy,
  // then it must NOT get a trailing slash.
  var last = srcPath.slice(-1)[0];
  var hasTrailingSlash = (
      (result.host || relative.host || srcPath.length > 1) &&
      (last === '.' || last === '..') || last === '');

  // strip single dots, resolve double dots to parent dir
  // if the path tries to go above the root, `up` ends up > 0
  var up = 0;
  for (var i = srcPath.length; i >= 0; i--) {
    last = srcPath[i];
    if (last === '.') {
      srcPath.splice(i, 1);
    } else if (last === '..') {
      srcPath.splice(i, 1);
      up++;
    } else if (up) {
      srcPath.splice(i, 1);
      up--;
    }
  }

  // if the path is allowed to go above the root, restore leading ..s
  if (!mustEndAbs && !removeAllDots) {
    for (; up--; up) {
      srcPath.unshift('..');
    }
  }

  if (mustEndAbs && srcPath[0] !== '' &&
      (!srcPath[0] || srcPath[0].charAt(0) !== '/')) {
    srcPath.unshift('');
  }

  if (hasTrailingSlash && (srcPath.join('/').substr(-1) !== '/')) {
    srcPath.push('');
  }

  var isAbsolute = srcPath[0] === '' ||
      (srcPath[0] && srcPath[0].charAt(0) === '/');

  // put the host back
  if (psychotic) {
    result.hostname = result.host = isAbsolute ? '' :
                                    srcPath.length ? srcPath.shift() : '';
    //occationaly the auth can get stuck only in host
    //this especially happens in cases like
    //url.resolveObject('mailto:local1@domain1', 'local2@domain2')
    var authInHost = result.host && result.host.indexOf('@') > 0 ?
                     result.host.split('@') : false;
    if (authInHost) {
      result.auth = authInHost.shift();
      result.host = result.hostname = authInHost.shift();
    }
  }

  mustEndAbs = mustEndAbs || (result.host && srcPath.length);

  if (mustEndAbs && !isAbsolute) {
    srcPath.unshift('');
  }

  if (!srcPath.length) {
    result.pathname = null;
    result.path = null;
  } else {
    result.pathname = srcPath.join('/');
  }

  //to support request.http
  if (!util.isNull(result.pathname) || !util.isNull(result.search)) {
    result.path = (result.pathname ? result.pathname : '') +
                  (result.search ? result.search : '');
  }
  result.auth = relative.auth || result.auth;
  result.slashes = result.slashes || relative.slashes;
  result.href = result.format();
  return result;
};

Url.prototype.parseHost = function() {
  var host = this.host;
  var port = portPattern.exec(host);
  if (port) {
    port = port[0];
    if (port !== ':') {
      this.port = port.substr(1);
    }
    host = host.substr(0, host.length - port.length);
  }
  if (host) this.hostname = host;
};

},{"./util":30,"punycode":25,"querystring":28}],30:[function(require,module,exports){
'use strict';

module.exports = {
  isString: function(arg) {
    return typeof(arg) === 'string';
  },
  isObject: function(arg) {
    return typeof(arg) === 'object' && arg !== null;
  },
  isNull: function(arg) {
    return arg === null;
  },
  isNullOrUndefined: function(arg) {
    return arg == null;
  }
};

},{}],"rhea":[function(require,module,exports){
(function (process){
/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'use strict';

var Connection = require('./connection.js');
var log = require('./log.js');
var rpc = require('./rpc.js');
var sasl = require('./sasl.js');
var util = require('./util.js');

var net = require("net");
var tls = require("tls");
var url = require("url");
var EventEmitter = require('events').EventEmitter;

var Container = function (options) {
    this.options = options ? Object.create(options) : {};
    if (!this.options.id) {
        this.options.id = util.generate_uuid();
    }
    this.id = this.options.id;
    this.sasl_server_mechanisms = sasl.server_mechanisms();
};

Container.prototype = Object.create(EventEmitter.prototype);
Container.prototype.constructor = Container;
Container.prototype.dispatch = function(name, context) {
    log.events('Container got event: ' + name);
    if (this.listeners(name).length) {
        EventEmitter.prototype.emit.apply(this, arguments);
        return true;
    } else {
        return false;
    }
};

Container.prototype.connect = function (options) {
    return new Connection(options, this).connect();
}
Container.prototype.listen = function (options) {
    var container = this;
    var server;
    if (options.transport === undefined || options.transport === 'tcp') {
        server = net.createServer();
        server.on('connection', function (socket) {
            new Connection(options, container).accept(socket);
        });
    } else if (options.transport === 'tls' || options.transport === 'ssl') {
        server = tls.createServer(options);
        server.on('secureConnection', function (socket) {
            new Connection(options, container).accept(socket);
        });
    } else {
        throw Error('Unrecognised transport: ' + options.transport);
    }
    if (process.version.match(/v0\.10\.\d+/)) {
        server.listen(options.port, options.host);
    } else {
        server.listen(options);
    }
    return server;
};
Container.prototype.create_container = function (options) {
    return new Container(options);
}
Container.prototype.get_option = function (name, default_value) {
    if (this.options[name] !== undefined) return this.options[name];
    else return default_value;
};
Container.prototype.generate_uuid = util.generate_uuid;
Container.prototype.rpc_server = function(address, options) { return rpc.server(this, address, options) };
Container.prototype.rpc_client = function(address) { return rpc.client(this, address) };
var ws = require('./ws.js');
Container.prototype.websocket_accept = function(socket, options) {
    new Connection(options, this).accept(ws.wrap(socket));
}
Container.prototype.websocket_connect = ws.connect;

module.exports = new Container();

}).call(this,require('_process'))
},{"./connection.js":1,"./log.js":5,"./rpc.js":7,"./sasl.js":8,"./util.js":13,"./ws.js":14,"_process":24,"events":23,"net":18,"tls":18,"url":29}]},{},[]);
