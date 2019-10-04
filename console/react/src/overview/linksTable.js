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

import { sortable } from "@patternfly/react-table";
import OverviewTableBase from "./overviewTableBase";

class LinksTable extends OverviewTableBase {
  constructor(props) {
    super(props);
    this.fields = [
      { title: "Link", field: "name" },
      { title: "Type", field: "linkType", transforms: [sortable] },
      { title: "Dir", field: "linkDir" },
      { title: "Admin status", field: "adminStatus" },
      { title: "Oper status", field: "operStatus" },
      { title: "Deliveries", field: "deliveryCount" },
      { title: "Rate", field: "rate" },
      { title: "Delayed 1 sec", field: "delayed1Sec" },
      { title: "Delayed 10 secs", field: "delayed10Sec" },
      { title: "Outstanding", field: "outstanding" },
      { title: "Address", field: "owningAddr", transforms: [sortable] }
    ];
  }
  doFetch = (page, perPage) => {
    return new Promise(resolve => {
      this.props.service.management.topology.fetchAllEntities(
        { entity: "router.link" },
        nodes => {
          // we have all the data now in the nodes object
          let linkFields = [];
          const now = new Date();
          var prettyVal = value => {
            return typeof value === "undefined"
              ? "-"
              : this.props.service.utilities.pretty(value);
          };
          var uncounts = link => {
            return this.props.service.utilities.pretty(
              link.undeliveredCount + link.unsettledCount
            );
          };
          var getLinkName = (node, link) => {
            let namestr = this.props.service.utilities.nameFromId(node);
            return `${namestr}:${link.identity}`;
          };
          var fixAddress = link => {
            let addresses = [];
            let owningAddr = link.owningAddr || "";
            let rawAddress = owningAddr;
            /*
               - "L*"  =>  "* (local)"
               - "M0*" =>  "* (direct)"
               - "M1*" =>  "* (dequeue)"
               - "MX*" =>  "* (phase X)"
          */
            let address = undefined;
            let starts = { L: "(local)", M0: "(direct)", M1: "(dequeue)" };
            for (let start in starts) {
              if (owningAddr.startsWith(start)) {
                let ends = owningAddr.substr(start.length);
                address = ends + " " + starts[start];
                rawAddress = ends;
                break;
              }
            }
            if (!address) {
              // check for MX*
              if (owningAddr.length > 3) {
                if (owningAddr[0] === "M") {
                  let phase = parseInt(owningAddr.substr(1));
                  if (phase && !isNaN(phase)) {
                    let phaseStr = phase + "";
                    let star = owningAddr.substr(2 + phaseStr.length);
                    address = `${star} (phase ${phaseStr})`;
                  }
                }
              }
            }
            addresses[0] = address || owningAddr;
            addresses[1] = rawAddress;
            return addresses;
          };
          for (let node in nodes) {
            const response = nodes[node]["router.link"];
            for (let i = 0; i < response.results.length; i++) {
              const result = response.results[i];
              const link = this.props.service.utilities.flatten(
                response.attributeNames,
                result
              );
              let linkName = getLinkName(node, link);
              let addresses = fixAddress(link);

              linkFields.push({
                link: linkName,
                title: linkName,
                outstanding: uncounts(link),
                operStatus: link.operStatus,
                adminStatus: link.adminStatus,
                owningAddr: addresses[0],

                acceptedCount: prettyVal(link.acceptedCount),
                modifiedCount: prettyVal(link.modifiedCount),
                presettledCount: prettyVal(link.presettledCount),
                rejectedCount: prettyVal(link.rejectedCount),
                releasedCount: prettyVal(link.releasedCount),
                deliveryCount: prettyVal(link.deliveryCount),

                rate: prettyVal(link.settleRate),
                deliveriesDelayed10Sec: prettyVal(link.deliveriesDelayed10Sec),
                deliveriesDelayed1Sec: prettyVal(link.deliveriesDelayed1Sec),
                capacity: link.capacity,
                undeliveredCount: link.undeliveredCount,
                unsettledCount: link.unsettledCount,

                rawAddress: addresses[1],
                rawDeliveryCount: link.deliveryCount,
                name: link.name,
                linkName: link.linkName,
                connectionId: link.connectionId,
                linkDir: link.linkDir,
                linkType: link.linkType,
                peer: link.peer,
                type: link.type,

                uid: linkName,
                timestamp: now,
                nodeId: node,
                identity: link.identity
              });
            }
          }
          resolve(this.slice(linkFields, page, perPage));
        }
      );
    });
  };
}

export default LinksTable;
