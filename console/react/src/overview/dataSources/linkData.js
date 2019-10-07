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

import React from "react";

const LinkDir = ({ value }) => (
  <span>
    <i
      className={`link-dir-${value} fa fa-arrow-circle-${
        value === "in" ? "right" : "left"
      }`}
    ></i>
    {value}
  </span>
);

class LinkData {
  constructor(service) {
    this.service = service;
    this.fields = [
      { title: "Link", field: "link", noWrap: true },
      { title: "Type", field: "linkType", noWrap: true },
      { title: "Dir", field: "linkDir", formatter: LinkDir },
      { title: "Admin status", field: "adminStatus" },
      { title: "Oper status", field: "operStatus" },
      {
        title: "Deliveries",
        field: "deliveryCount",
        noWrap: true,
        numeric: true
      },
      { title: "Rate", field: "rate", numeric: true },
      {
        title: "Delayed 1 sec",
        field: "delayed1Sec",
        numeric: true
      },
      {
        title: "Delayed 10 secs",
        field: "delayed10Sec",
        numeric: true
      },
      {
        title: "Outstanding",
        field: "outstanding",
        numeric: true
      },
      { title: "Address", field: "owningAddr" }
    ];
    this.detailEntity = "router.link";
    this.detailName = "Link";
  }

  fetchRecord = (currentRecord, schema) => {
    return new Promise(resolve => {
      this.service.management.topology.fetchEntities(
        currentRecord.nodeId,
        [{ entity: "router.link" }],
        data => {
          const record = data[currentRecord.nodeId]["router.link"];
          const identityIndex = record.attributeNames.indexOf("identity");
          const result = record.results.find(
            r => r[identityIndex] === currentRecord.identity
          );
          let link = this.service.utilities.flatten(
            record.attributeNames,
            result
          );
          link = this.service.utilities.formatAttributes(
            link,
            schema.entityTypes["router.link"]
          );
          resolve(link);
        }
      );
    });
  };

  doFetch = (page, perPage) => {
    return new Promise(resolve => {
      this.service.management.topology.fetchAllEntities(
        { entity: "router.link" },
        nodes => {
          // we have all the data now in the nodes object
          let linkFields = [];
          var getLinkName = (node, link) => {
            let namestr = this.service.utilities.nameFromId(node);
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
              const link = this.service.utilities.flatten(
                response.attributeNames,
                result
              );
              let linkName = getLinkName(node, link);
              let addresses = fixAddress(link);

              linkFields.push({
                link: linkName,
                linkType: link.linkType,
                linkDir: link.linkDir,
                adminStatus: link.adminStatus,
                operStatus: link.operStatus,
                deliveryCount: link.deliveryCount,
                rate: link.settleRate,
                delayed1Sec: link.deliveriesDelayed1Sec,
                delayed10Sec: link.deliveriesDelayed10Sec,
                outstanding: link.undeliveredCount + link.unsettledCount,
                owningAddr: addresses[0],

                capacity: link.capacity,
                undeliveredCount: link.undeliveredCount,
                unsettledCount: link.unsettledCount,

                rawAddress: addresses[1],
                name: link.name,
                connectionId: link.connectionId,
                peer: link.peer,
                type: link.type,

                nodeId: node,
                identity: link.identity
              });
            }
          }
          resolve({ data: linkFields, page, perPage });
        }
      );
    });
  };
}

export default LinkData;
