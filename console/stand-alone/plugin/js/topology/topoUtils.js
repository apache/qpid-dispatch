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

/* global Set */
import {
  utils
} from "../amqp/utilities.js";

// highlight the paths between the selected node and the hovered node
function findNextHopNode(from, d, nodeInfo, selected_node, nodes) {
  // d is the node that the mouse is over
  // from is the selected_node ....
  if (!from) return null;

  if (from == d) return selected_node;

  let sInfo = nodeInfo[from.key];

  // find the hovered name in the selected name's .router.node results
  if (!sInfo["router.node"]) return null;
  let aAr = sInfo["router.node"].attributeNames;
  let vAr = sInfo["router.node"].results;
  for (let hIdx = 0; hIdx < vAr.length; ++hIdx) {
    let addrT = utils.valFor(aAr, vAr[hIdx], "id");
    if (d.name && addrT == d.name) {
      let next = utils.valFor(aAr, vAr[hIdx], "nextHop");
      return next == null ? nodes.nodeFor(addrT) : nodes.nodeFor(next);
    }
  }
  return null;
}
export function nextHop(
  thisNode,
  d,
  nodes,
  links,
  nodeInfo,
  selected_node,
  cb
) {
  if (thisNode && thisNode != d) {
    let target = findNextHopNode(thisNode, d, nodeInfo, selected_node, nodes);
    if (target) {
      let hnode = nodes.nodeFor(thisNode.name);
      let hlLink = links.linkFor(hnode, target);
      if (hlLink) {
        if (cb) {
          cb(hlLink, hnode, target);
        }
      } else target = null;
    }
    nextHop(target, d, nodes, links, nodeInfo, selected_node, cb);
  }
}

let linkRateHistory = {};
export function connectionPopupHTML(d, nodeInfo) {
  if (!d) {
    linkRateHistory = {};
    return;
  }
  // return all of onode's connections that connecto to right
  let getConnsArray = function (onode, key, right) {
    if (right.normals) {
      // if we want connections between a router and a client[s]
      let connIds = new Set();
      let connIndex = onode.connection.attributeNames.indexOf("identity");
      for (let n = 0; n < right.normals.length; n++) {
        let normal = right.normals[n];
        if (normal.key === key) {
          connIds.add(normal.connectionId);
        } else if (normal.alsoConnectsTo) {
          normal.alsoConnectsTo.forEach(function (ac2) {
            if (ac2.key === key) connIds.add(ac2.connectionId);
          });
        }
      }
      return onode.connection.results
        .filter(function (result) {
          return connIds.has(result[connIndex]);
        })
        .map(function (c) {
          return utils.flatten(onode.connection.attributeNames, c);
        });
    } else {
      // we want the connection between two routers
      let container = utils.nameFromId(right.key);
      let containerIndex = onode.connection.attributeNames.indexOf("container");
      let roleIndex = onode.connection.attributeNames.indexOf("role");
      return onode.connection.results
        .filter(function (conn) {
          return (
            conn[containerIndex] === container &&
            conn[roleIndex] === "inter-router"
          );
        })
        .map(function (c) {
          return utils.flatten(onode.connection.attributeNames, c);
        });
    }
  };
  // construct HTML to be used in a popup when the mouse is moved over a link.
  // The HTML is sanitized elsewhere before it is displayed
  let linksHTML = function (onode, conns) {
    const max_links = 10;
    const fields = [
      "deliveryCount",
      "undeliveredCount",
      "unsettledCount",
      "rejectedCount",
      "releasedCount",
      "modifiedCount"
    ];
    // local function to determine if a link's connectionId is in any of the connections
    let isLinkFor = function (connectionId, conns) {
      for (let c = 0; c < conns.length; c++) {
        if (conns[c].identity === connectionId) return true;
      }
      return false;
    };
    let fnJoin = function (ar, sepfn) {
      let out = "";
      out = ar[0];
      for (let i = 1; i < ar.length; i++) {
        let sep = sepfn(ar[i], i === ar.length - 1);
        out += sep[0] + sep[1];
      }
      return out;
    };
    // if the data for the line is from a client (small circle), we may have multiple connections
    // loop through all links for this router and accumulate those belonging to the connection(s)
    let nodeLinks = onode["router.link"];
    if (!nodeLinks) return "";
    let links = [];
    let hasAddress = false;
    for (let n = 0; n < nodeLinks.results.length; n++) {
      let link = utils.flatten(nodeLinks.attributeNames, nodeLinks.results[n]);
      let allZero = true;
      if (link.linkType !== "router-control") {
        if (isLinkFor(link.connectionId, conns)) {
          if (link.owningAddr) hasAddress = true;
          if (link.name) {
            let rates = utils.rates(
              link,
              fields,
              linkRateHistory,
              link.name,
              1
            );
            // replace the raw value with the rate
            for (let i = 0; i < fields.length; i++) {
              if (rates[fields[i]] > 0) allZero = false;
              link[fields[i]] = rates[fields[i]];
            }
          }
          if (!allZero) links.push(link);
        }
      }
    }
    // we may need to limit the number of links displayed, so sort descending by the sum of the field values
    let sum = function (a) {
      let s = 0;
      for (let i = 0; i < fields.length; i++) {
        s += a[fields[i]];
      }
      return s;
    };
    links.sort(function (a, b) {
      let asum = sum(a);
      let bsum = sum(b);
      return asum < bsum ? 1 : asum > bsum ? -1 : 0;
    });

    let HTMLHeading = "<h5>Rates (per second) for links</h5>";
    let HTML = '<table class="popupTable">';
    // copy of fields since we may be prepending an address
    let th = fields.slice();
    let td = fields;
    th.unshift("dir");
    td.unshift("linkDir");
    th.push("priority");
    td.push("priority");
    // add an address field if any of the links had an owningAddress
    if (hasAddress) {
      th.unshift("address");
      td.unshift("owningAddr");
    }

    let rate_th = function (th) {
      let rth = th.map(function (t) {
        if (t.endsWith("Count")) t = t.replace("Count", "Rate");
        return utils.humanify(t);
      });
      return rth;
    };
    HTML +=
      '<tr class="header"><td>' + rate_th(th).join("</td><td>") + "</td></tr>";
    // add rows to the table for each link
    for (let l = 0; l < links.length; l++) {
      if (l >= max_links) {
        HTMLHeading = `<h5>Rates (per second) for top ${max_links} links</h5>`;
        break;
      }
      let link = links[l];
      let vals = td.map(function (f) {
        if (f === "owningAddr") {
          let identity = utils.identity_clean(link.owningAddr);
          return utils.addr_text(identity);
        }
        return link[f];
      });
      let joinedVals = fnJoin(vals, function (v1, last) {
        return [
          `</td><td${isNaN(+v1) ? "" : ' align="right"'}>`,
          last ? v1 : utils.pretty(v1 || "0", ",.2f")
        ];
      });
      HTML += `<tr><td> ${joinedVals} </td></tr>`;
    }
    return links.length > 0 ? `${HTMLHeading}${HTML}</table>` : "";
  };

  let left, right;
  if (d.left) {
    left = d.source;
    right = d.target;
  } else {
    left = d.target;
    right = d.source;
  }
  if (left.normals) {
    // swap left and right
    [left, right] = [right, left];
  }
  // left is a router. right is either a router or a client[s]
  let onode = nodeInfo[left.key];
  // find all the connections for left that go to right
  let conns = getConnsArray(onode, left.key, right);

  let HTML = "";
  HTML += "<h5>Connection" + (conns.length > 1 ? "s" : "") + "</h5>";
  HTML +=
    '<table class="popupTable"><tr class="header"><td>Security</td><td>Authentication</td><td>Tenant</td><td>Host</td>';

  for (let c = 0; c < Math.min(conns.length, 10); c++) {
    HTML += "<tr><td>" + utils.connSecurity(conns[c]) + "</td>";
    HTML += "<td>" + utils.connAuth(conns[c]) + "</td>";
    HTML += "<td>" + (utils.connTenant(conns[c]) || "--") + "</td>";
    HTML += "<td>" + conns[c].host + "</td>";
    HTML += "</tr>";
  }
  HTML += "</table>";
  HTML += linksHTML(onode, conns);
  return HTML;
}

export function getSizes(QDRLog) {
  const gap = 5;
  let legendWidth = 194;
  let topoWidth = $("#topology").width();
  if (topoWidth < 768) legendWidth = 0;
  let width = $("#topology").width() - gap - legendWidth;
  let top = $("#topology").offset().top;
  let height = window.innerHeight - top - gap;
  if (width < 10) {
    QDRLog.info(
      `page width and height are abnormal w: ${width} h: ${height}`
    );
    return [0, 0];
  }
  return [width, height];
}
