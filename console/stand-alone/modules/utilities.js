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

/* global d3 */
var ddd = typeof window === 'undefined' ? require ('d3') : d3;

var utils = {
  isAConsole: function (properties, connectionId, nodeType, key) {
    return this.isConsole({
      properties: properties,
      connectionId: connectionId,
      nodeType: nodeType,
      key: key
    });
  },
  isConsole: function (d) {
    return (d && d.properties && d.properties.console_identifier === 'Dispatch console');
  },
  isArtemis: function (d) {
    return (d.nodeType === 'route-container' || d.nodeType === 'on-demand') && (d.properties && d.properties.product === 'apache-activemq-artemis');
  },

  isQpid: function (d) {
    return (d.nodeType === 'route-container' || d.nodeType === 'on-demand') && (d.properties && d.properties.product === 'qpid-cpp');
  },
  flatten: function (attributes, result) {
    if (!attributes || !result)
      return {};
    var flat = {};
    attributes.forEach(function(attr, i) {
      if (result && result.length > i)
        flat[attr] = result[i];
    });
    return flat;
  },
  copy: function (obj) {
    if (obj)
      return JSON.parse(JSON.stringify(obj));
  },
  identity_clean: function (identity) {
    if (!identity)
      return '-';
    var pos = identity.indexOf('/');
    if (pos >= 0)
      return identity.substring(pos + 1);
    return identity;
  },
  addr_text: function (addr) {
    if (!addr)
      return '-';
    if (addr[0] == 'M')
      return addr.substring(2);
    else
      return addr.substring(1);
  },
  addr_class: function (addr) {
    if (!addr) return '-';
    if (addr[0] == 'M') return 'mobile';
    if (addr[0] == 'R') return 'router';
    if (addr[0] == 'A') return 'area';
    if (addr[0] == 'L') return 'local';
    if (addr[0] == 'C') return 'link-incoming';
    if (addr[0] == 'E') return 'link-incoming';
    if (addr[0] == 'D') return 'link-outgoing';
    if (addr[0] == 'F') return 'link-outgoing';
    if (addr[0] == 'T') return 'topo';
    return 'unknown: ' + addr[0];
  },
  humanify: function (s) {
    if (!s || s.length === 0)
      return s;
    var t = s.charAt(0).toUpperCase() + s.substr(1).replace(/[A-Z]/g, ' $&');
    return t.replace('.', ' ');
  },
  pretty: function (v) {
    var formatComma = ddd.format(',');
    if (!isNaN(parseFloat(v)) && isFinite(v))
      return formatComma(v);
    return v;
  },
  isMSIE: function () {
    return (document.documentMode || /Edge/.test(navigator.userAgent));
  },
  valFor: function (aAr, vAr, key) {
    var idx = aAr.indexOf(key);
    if ((idx > -1) && (idx < vAr.length)) {
      return vAr[idx];
    }
    return null;
  },
  // extract the name of the router from the router id
  nameFromId: function (id) {
    // the router id looks like 'amqp:/topo/0/routerName/$managemrnt'
    var parts = id.split('/');
    // handle cases where the router name contains a /
    parts.splice(0, 3); // remove amqp, topo, 0
    parts.pop(); // remove $management
    return parts.join('/');
  }
  
};
export { utils };