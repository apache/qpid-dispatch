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

import * as d3 from "d3";

var utils = {
  isAConsole: function(properties, connectionId, nodeType, key) {
    return utils.isConsole({
      properties: properties,
      connectionId: connectionId,
      nodeType: nodeType,
      key: key
    });
  },
  isConsole: function(d) {
    return d && d.properties && d.properties.console_identifier === "Dispatch console";
  },
  isArtemis: function(d) {
    return (
      (d.nodeType === "route-container" || d.nodeType === "on-demand") &&
      d.properties &&
      d.properties.product === "apache-activemq-artemis"
    );
  },

  isQpid: function(d) {
    return (
      (d.nodeType === "route-container" || d.nodeType === "on-demand") &&
      d.properties &&
      d.properties.product === "qpid-cpp"
    );
  },

  clientName: function(d) {
    let name = "client";
    if (d.container) name = d.container;
    if (d.properties) {
      if (d.properties.product) name = d.properties.product;
      else if (d.properties.console_identifier) name = d.properties.console_identifier;
      else if (d.properties.name) name = d.properties.name;
    }
    return name;
  },
  flatten: function(attributes, result) {
    if (!attributes || !result) return {};
    var flat = {};
    attributes.forEach(function(attr, i) {
      if (result && result.length > i) flat[attr] = result[i];
    });
    return flat;
  },
  flattenAll: function(entity, filter) {
    if (!filter)
      filter = function(e) {
        return e;
      };
    let results = [];
    for (let i = 0; i < entity.results.length; i++) {
      let f = filter(utils.flatten(entity.attributeNames, entity.results[i]));
      if (f) results.push(f);
    }
    return results;
  },
  copy: function(obj) {
    if (obj) return JSON.parse(JSON.stringify(obj));
  },
  identity_clean: function(identity) {
    if (!identity) return "-";
    var pos = identity.indexOf("/");
    if (pos >= 0) return identity.substring(pos + 1);
    return identity;
  },
  addr_text: function(addr) {
    if (!addr) return "-";
    if (addr[0] === addr[0].toLowerCase()) return addr;
    if (addr[0] === "M") return addr.substring(2);
    else return addr.substring(1);
  },
  addr_class: function(addr) {
    if (!addr) return "-";
    if (addr[0] === "M") return "mobile";
    if (addr[0] === "R") return "router";
    if (addr[0] === "A") return "area";
    if (addr[0] === "L") return "local";
    if (addr[0] === "H") return "edge";
    if (addr[0] === "C") return "link-incoming";
    if (addr[0] === "E") return "link-incoming";
    if (addr[0] === "D") return "link-outgoing";
    if (addr[0] === "F") return "link-outgoing";
    if (addr[0] === "T") return "topo";
    if (addr === "queue.waypoint") return "mobile";
    if (addr === "link") return "link";
    return "unknown: " + addr[0];
  },
  humanify: function(s) {
    if (!s || s.length === 0) return s;
    var t = s.charAt(0).toUpperCase() + s.substr(1).replace(/[A-Z]/g, " $&");
    return t.replace(".", " ");
  },
  pretty: function(v, format = ",") {
    var formatComma = d3.format(format);
    if (!isNaN(parseFloat(v)) && isFinite(v)) return formatComma(v);
    return v;
  },
  strDate: function(date) {
    return `${(date.getHours() + "").padStart(2, "0")}:${(
      date.getMinutes() + ""
    ).padStart(2, "0")}:${(date.getSeconds() + "").padStart(2, "0")}`;
  },
  isMSIE: function() {
    return document.documentMode || /Edge/.test(navigator.userAgent);
  },
  // return the value for a field
  valFor: function(aAr, vAr, key) {
    var idx = aAr.indexOf(key);
    if (idx > -1 && idx < vAr.length) {
      return vAr[idx];
    }
    return null;
  },
  // return a map with unique values and their counts for a field
  countsFor: function(aAr, vAr, key) {
    let counts = {};
    let idx = aAr.indexOf(key);
    for (let i = 0; i < vAr.length; i++) {
      if (!counts[vAr[i][idx]]) counts[vAr[i][idx]] = 0;
      counts[vAr[i][idx]]++;
    }
    return counts;
  },
  // extract the name of the router from the router id
  nameFromId: function(id) {
    // the router id looks like
    //  amqp:/_topo/0/routerName/$management'
    //  amqp:/_topo/0/router/Name/$management'
    //  amqp:/_edge/routerName/$management'
    //  amqp:/_edge/router/Name/$management'

    var parts = id.split("/");
    // remove $management
    parts.pop();

    // remove the area if present
    if (parts[2] === "0") parts.splice(2, 1);

    // remove amqp/(_topo or _edge)
    parts.splice(0, 2);
    return parts.join("/");
  },

  // construct a router id given a router name and type (_topo or _edge)
  idFromName: function(name, type) {
    let parts = ["amqp:", type, name, "$management"];
    if (type === "_topo") parts.splice(2, 0, "0");
    return parts.join("/");
  },

  typeFromId: function(id) {
    var parts = id.split("/");
    if (parts.length > 1) return parts[1];
    return "unknown";
  },

  // calculate the average rate of change per second for a list of fields on the given obj
  // store the historical raw values in storage[key] for future rate calcs
  // keep 'history' number of historical values
  rates: function(obj, fields, storage, key, history = 1) {
    let list = storage[key];
    if (!list) {
      list = storage[key] = [];
    }
    // expire old entries
    while (list.length > history) {
      list.shift();
    }
    let rates = {};
    list.push({
      date: new Date(),
      val: Object.assign({}, obj)
    });

    for (let i = 0; i < fields.length; i++) {
      let cumulative = 0;
      let field = fields[i];
      for (let j = 0; j < list.length - 1; j++) {
        let elapsed = list[j + 1].date - list[j].date;
        let diff = list[j + 1].val[field] - list[j].val[field];
        if (elapsed > 100) cumulative += diff / (elapsed / 1000);
      }
      rates[field] = list.length > 1 ? cumulative / (list.length - 1) : 0;
    }
    return rates;
  },
  connSecurity: function(conn) {
    if (!conn.isEncrypted) return "no-security";
    if (conn.sasl === "GSSAPI") return "Kerberos";
    return conn.sslProto + "(" + conn.sslCipher + ")";
  },
  connAuth: function(conn) {
    if (!conn.isAuthenticated) return "no-auth";
    let sasl = conn.sasl;
    if (sasl === "GSSAPI") sasl = "Kerberos";
    else if (sasl === "EXTERNAL") sasl = "x.509";
    else if (sasl === "ANONYMOUS") return "anonymous-user";
    if (!conn.user) return sasl;
    return conn.user + "(" + sasl + ")";
  },
  connTenant: function(conn) {
    if (!conn.tenant) {
      return "";
    }
    if (conn.tenant.length > 1) return conn.tenant.replace(/\/$/, "");
  },
  uuidv4: function() {
    return ([1e7] + -1e3 + -4e3 + -8e3 + -1e11).replace(/[018]/g, c =>
      (c ^ (crypto.getRandomValues(new Uint8Array(1))[0] & (15 >> (c / 4)))).toString(16)
    );
  },
  getUrlParts: function(fullUrl) {
    fullUrl = fullUrl || window.location;
    const url = document.createElement("a");
    url.setAttribute("href", fullUrl);
    return url;
  },
  Icap: s => s[0].toUpperCase() + s.slice(1),

  // get last token in string that looks like "/fooo/baaar/baaaz"
  entityFromProps: props => {
    if (props && props.location && props.location.pathname) {
      return props.location.pathname.split("/").slice(-1)[0];
    }
    return "";
  },

  formatAttributes: (record, entityType) => {
    for (const attrib in record) {
      const schemaAttrib = entityType.attributes[attrib];
      if (schemaAttrib) {
        if (schemaAttrib.type === "integer") {
          record[attrib] = utils.pretty(record[attrib]);
        } else if (record[attrib] === null) {
          record[attrib] = "";
        } else if (schemaAttrib.type === "map") {
          record[attrib] = JSON.stringify(record[attrib], null, 2);
        } else {
          record[attrib] = String(record[attrib]);
        }
      }
    }
    return record;
  }
};

export { utils };
