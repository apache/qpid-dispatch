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
// highlight the paths between the selected node and the hovered node
function findNextHopNode(from, d, QDRService, selected_node, nodes) {
  // d is the node that the mouse is over
  // from is the selected_node ....
  if (!from)
    return null;

  if (from == d)
    return selected_node;

  let sInfo = QDRService.management.topology.nodeInfo()[from.key];

  // find the hovered name in the selected name's .router.node results
  if (!sInfo['router.node'])
    return null;
  let aAr = sInfo['router.node'].attributeNames;
  let vAr = sInfo['router.node'].results;
  for (let hIdx = 0; hIdx < vAr.length; ++hIdx) {
    let addrT = QDRService.utilities.valFor(aAr, vAr[hIdx], 'id');
    if (addrT == d.name) {
      let next = QDRService.utilities.valFor(aAr, vAr[hIdx], 'nextHop');
      return (next == null) ? nodes.nodeFor(addrT) : nodes.nodeFor(next);
    }
  }
  return null;
}
export function nextHop(thisNode, d, nodes, links, QDRService, selected_node, cb) {
  if ((thisNode) && (thisNode != d)) {
    let target = findNextHopNode(thisNode, d, QDRService, selected_node, nodes);
    if (target) {
      let hnode = nodes.nodeFor(thisNode.name);
      let hlLink = links.linkFor(hnode, target);
      if (hlLink) {
        if (cb) {
          cb(hlLink, hnode, target);
        }
      }
      else
        target = null;
    }
    nextHop(target, d, nodes, links, QDRService, selected_node, cb);
  }
}

export function connectionPopupHTML (d, QDRService) {
  let getConnsArray = function (d, conn) {
    let conns = [conn];
    if (d.cls === 'small') {
      conns = [];
      let normals = d.target.normals ? d.target.normals : d.source.normals;
      for (let n=0; n<normals.length; n++) {
        if (normals[n].resultIndex !== undefined) {
          conns.push(QDRService.utilities.flatten(onode['connection'].attributeNames,
            onode['connection'].results[normals[n].resultIndex]));
        }
      }
    }
    return conns;
  };
  // construct HTML to be used in a popup when the mouse is moved over a link.
  // The HTML is sanitized elsewhere before it is displayed
  let linksHTML = function (onode, conn, d) {
    const max_links = 10;
    const fields = ['undelivered', 'unsettled', 'rejected', 'released', 'modified'];
    // local function to determine if a link's connectionId is in any of the connections
    let isLinkFor = function (connectionId, conns) {
      for (let c=0; c<conns.length; c++) {
        if (conns[c].identity === connectionId)
          return true;
      }
      return false;
    };
    let fnJoin = function (ar, sepfn) {
      let out = '';
      out = ar[0];
      for (let i=1; i<ar.length; i++) {
        let sep = sepfn(ar[i]);
        out += (sep[0] + sep[1]);
      }
      return out;
    };
    let conns = getConnsArray(d, conn);
    // if the data for the line is from a client (small circle), we may have multiple connections
    // loop through all links for this router and accumulate those belonging to the connection(s)
    let nodeLinks = onode['router.link'];
    if (!nodeLinks)
      return '';
    let links = [];
    let hasAddress = false;
    for (let n=0; n<nodeLinks.results.length; n++) {
      let link = QDRService.utilities.flatten(nodeLinks.attributeNames, nodeLinks.results[n]);
      if (link.linkType !== 'router-control') {
        if (isLinkFor(link.connectionId, conns)) {
          if (link.owningAddr)
            hasAddress = true;
          links.push(link);
        }
      }
    }
    // we may need to limit the number of links displayed, so sort descending by the sum of the field values
    links.sort( function (a, b) {
      let asum = a.undeliveredCount + a.unsettledCount + a.rejectedCount + a.releasedCount + a.modifiedCount;
      let bsum = b.undeliveredCount + b.unsettledCount + b.rejectedCount + b.releasedCount + b.modifiedCount;
      return asum < bsum ? 1 : asum > bsum ? -1 : 0;
    });
    let HTMLHeading = '<h5>Links</h5>';
    let HTML = '<table class="popupTable">';
    // copy of fields since we may be prepending an address
    let th = fields.slice();
    // convert to actual attribute names
    let td = fields.map( function (f) {return f + 'Count';});
    th.unshift('dir');
    td.unshift('linkDir');
    // add an address field if any of the links had an owningAddress
    if (hasAddress) {
      th.unshift('address');
      td.unshift('owningAddr');
    }
    HTML += ('<tr class="header"><td>' + th.join('</td><td>') + '</td></tr>');
    // add rows to the table for each link
    for (let l=0; l<links.length; l++) {
      if (l>=max_links) {
        HTMLHeading = `<h4>Top ${max_links} Links</h4>`;
        break;
      }
      let link = links[l];
      let vals = td.map( function (f) {
        if (f === 'owningAddr') {
          let identity = QDRService.utilities.identity_clean(link.owningAddr);
          return QDRService.utilities.addr_text(identity);
        }
        return link[f];
      });
      let joinedVals = fnJoin(vals, function (v1) {
        return ['</td><td' + (isNaN(+v1) ? '': ' align="right"') + '>', QDRService.utilities.pretty(v1 || '0')];
      });
      HTML += `<tr><td> ${joinedVals} </td></tr>`;
    }
    HTML += '</table>';
    return HTMLHeading + HTML;
  };
  let left = d.left ? d.source : d.target;
  // left is the connection with dir 'in'
  let right = d.left ? d.target : d.source;
  let onode = QDRService.management.topology.nodeInfo()[left.key];
  let connSecurity = function (conn) {
    if (!conn.isEncrypted)
      return 'no-security';
    if (conn.sasl === 'GSSAPI')
      return 'Kerberos';
    return conn.sslProto + '(' + conn.sslCipher + ')';
  };
  let connAuth = function (conn) {
    if (!conn.isAuthenticated)
      return 'no-auth';
    let sasl = conn.sasl;
    if (sasl === 'GSSAPI')
      sasl = 'Kerberos';
    else if (sasl === 'EXTERNAL')
      sasl = 'x.509';
    else if (sasl === 'ANONYMOUS')
      return 'anonymous-user';
    if (!conn.user)
      return sasl;
    return conn.user + '(' + sasl + ')';
  };
  let connTenant = function (conn) {
    if (!conn.tenant) {
      return '';
    }
    if (conn.tenant.length > 1)
      return conn.tenant.replace(/\/$/, '');
  };
  // loop through all the connections for left, and find the one for right
  let rightIndex = onode['connection'].results.findIndex( function (conn) {
    return QDRService.utilities.valFor(onode['connection'].attributeNames, conn, 'container') === right.routerId;
  });
  if (rightIndex < 0) {
    // we have a connection to a client/service
    rightIndex = +left.resultIndex;
  }
  if (isNaN(rightIndex)) {
    // we have a connection to a console
    rightIndex = +right.resultIndex;
  }
  let HTML = '';
  if (rightIndex >= 0) {
    let conn = onode['connection'].results[rightIndex];
    conn = QDRService.utilities.flatten(onode['connection'].attributeNames, conn);
    let conns = getConnsArray(d, conn);
    if (conns.length === 1) {
      HTML += '<h5>Connection'+(conns.length > 1 ? 's' : '')+'</h5>';
      HTML += '<table class="popupTable"><tr class="header"><td>Security</td><td>Authentication</td><td>Tenant</td><td>Host</td>';

      for (let c=0; c<conns.length; c++) {
        HTML += ('<tr><td>' + connSecurity(conns[c]) + '</td>');
        HTML += ('<td>' + connAuth(conns[c]) + '</td>');
        HTML += ('<td>' + (connTenant(conns[c]) || '--') + '</td>');
        HTML += ('<td>' + conns[c].host + '</td>');
        HTML += '</tr>';
      }
      HTML += '</table>';
    }
    HTML += linksHTML(onode, conn, d);
  }
  return HTML;
}
