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

import BaseHelper from "./baseHelper";

class AddressHelper extends BaseHelper {
  constructor(service) {
    super(service);
    this.fields = [
      { title: "Address", field: "address", transforms: [sortable] },
      { title: "Class", field: "class" },
      { title: "Phase", field: "phase" },
      { title: "In-proc", field: "inproc" },
      { title: "Local", field: "local" },
      { title: "Remote", field: "remote" },
      { title: "In", field: "in" },
      { title: "Out", field: "out" }
    ];
  }

  fetch = (perPage, page, sortBy) => {
    return new Promise(resolve => {
      var addr_phase = addr => {
        if (!addr) return "-";
        if (addr[0] === "M") return addr[1];
        return "";
      };
      var prettyVal = val => {
        return this.service.utilities.pretty(val || "-");
      };
      let addressFields = [];
      let addressObjs = {};
      // send the requests for all connection and router info for all routers
      this.service.management.topology.fetchAllEntities(
        { entity: "router.address" },
        nodes => {
          for (let node in nodes) {
            let response = nodes[node]["router.address"];
            response.results.forEach(result => {
              let address = this.service.utilities.flatten(
                response.attributeNames,
                result
              );

              var addNull = (oldVal, newVal) => {
                if (oldVal != null && newVal != null) return oldVal + newVal;
                if (oldVal != null) return oldVal;
                return newVal;
              };

              let uid = address.identity;
              let identity = this.service.utilities.identity_clean(uid);

              if (
                !addressObjs[
                  this.service.utilities.addr_text(identity) +
                    this.service.utilities.addr_class(identity)
                ]
              )
                addressObjs[
                  this.service.utilities.addr_text(identity) +
                    this.service.utilities.addr_class(identity)
                ] = {
                  address: this.service.utilities.addr_text(identity),
                  class: this.service.utilities.addr_class(identity),
                  phase: addr_phase(identity),
                  inproc: address.inProcess,
                  local: address.subscriberCount,
                  remote: address.remoteCount,
                  in: address.deliveriesIngress,
                  out: address.deliveriesEgress,
                  thru: address.deliveriesTransit,
                  toproc: address.deliveriesToContainer,
                  fromproc: address.deliveriesFromContainer,
                  uid: uid
                };
              else {
                let sumObj =
                  addressObjs[
                    this.service.utilities.addr_text(identity) +
                      this.service.utilities.addr_class(identity)
                  ];
                sumObj.inproc = addNull(sumObj.inproc, address.inProcess);
                sumObj.local = addNull(sumObj.local, address.subscriberCount);
                sumObj.remote = addNull(sumObj.remote, address.remoteCount);
                sumObj["in"] = addNull(sumObj["in"], address.deliveriesIngress);
                sumObj.out = addNull(sumObj.out, address.deliveriesEgress);
                sumObj.thru = addNull(sumObj.thru, address.deliveriesTransit);
                sumObj.toproc = addNull(
                  sumObj.toproc,
                  address.deliveriesToContainer
                );
                sumObj.fromproc = addNull(
                  sumObj.fromproc,
                  address.deliveriesFromContainer
                );
              }
            });
          }
          for (let obj in addressObjs) {
            addressObjs[obj].inproc = prettyVal(addressObjs[obj].inproc);
            addressObjs[obj].local = prettyVal(addressObjs[obj].local);
            addressObjs[obj].remote = prettyVal(addressObjs[obj].remote);
            addressObjs[obj]["in"] = prettyVal(addressObjs[obj]["in"]);
            addressObjs[obj].out = prettyVal(addressObjs[obj].out);
            addressObjs[obj].thru = prettyVal(addressObjs[obj].thru);
            addressObjs[obj].toproc = prettyVal(addressObjs[obj].toproc);
            addressObjs[obj].fromproc = prettyVal(addressObjs[obj].fromproc);
            addressFields.push(addressObjs[obj]);
          }
          if (addressFields.length === 0) return;
          // update the grid's data
          addressFields.sort((a, b) => {
            return a.address + a["class"] < b.address + b["class"]
              ? -1
              : a.address + a["class"] > b.address + b["class"]
              ? 1
              : 0;
          });
          addressFields[0].title = addressFields[0].address;
          for (let i = 1; i < addressFields.length; ++i) {
            // if this address is the same as the previous address, add a class to the display titles
            if (addressFields[i].address === addressFields[i - 1].address) {
              addressFields[i - 1].title =
                addressFields[i - 1].address +
                " (" +
                addressFields[i - 1]["class"] +
                ")";
              addressFields[i].title =
                addressFields[i].address +
                " (" +
                addressFields[i]["class"] +
                ")";
            } else addressFields[i].title = addressFields[i].address;
          }
          resolve(this.slice(addressFields, page, perPage, sortBy));
        }
      );
    });
  };
}
export default AddressHelper;
