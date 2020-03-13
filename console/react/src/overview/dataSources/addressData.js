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

class AddressData {
  constructor(service) {
    this.service = service;
    this.fields = [
      { title: "Address", field: "address" },
      { title: "Class", field: "class" },
      { title: "Phase", field: "phase" },
      { title: "Distribution", field: "distribution" },
      { title: "In-proc", field: "inproc", numeric: true },
      { title: "Local", field: "local", numeric: true },
      { title: "Remote", field: "remote", numeric: true },
      { title: "In", field: "in", numeric: true },
      { title: "Out", field: "out", numeric: true }
    ];
    this.detailName = "Address";
    this.detailField = "title";
    this.hideFields = ["title"];
  }

  fetchRecord = (currentRecord, schema) => {
    return new Promise(resolve => {
      this.service.management.topology.fetchEntities(
        currentRecord.nodeId,
        [{ entity: "router.address" }],
        data => {
          const record = data[currentRecord.nodeId]["router.address"];
          const identityIndex = record.attributeNames.indexOf("identity");
          const result = record.results.find(r => r[identityIndex] === currentRecord.uid);
          let address = this.service.utilities.flatten(record.attributeNames, result);
          address = this.service.utilities.formatAttributes(
            address,
            schema.entityTypes["router.address"]
          );
          resolve(address);
        }
      );
    });
  };

  doFetch = (page, perPage) => {
    return new Promise(resolve => {
      var addr_phase = addr => {
        if (!addr) return "-";
        if (addr[0] === "M") return addr[1];
        return "";
      };
      let addressFields = [];
      let addressObjs = {};
      var addNull = (oldVal, newVal) => {
        if (oldVal != null && newVal != null) return oldVal + newVal;
        if (oldVal != null) return oldVal;
        return newVal;
      };
      // send the requests to get the router.address records for every router
      this.service.management.topology.fetchAllEntities(
        { entity: "router.address" },
        nodes => {
          // each router is a node in nodes
          for (let node in nodes) {
            let response = nodes[node]["router.address"];
            // response is an array of router.address records for this node/router
            response.results.forEach(result => {
              // result is a single address record for this node/router
              let address = this.service.utilities.flatten(
                response.attributeNames,
                result
              );
              // address is now an object with attribute names as keys and their values
              let uid = address.identity;
              //let identity = this.service.utilities.identity_clean(uid);
              let identity = address.identity;
              // if this is the 1st time we've seen this address:class
              if (
                !addressObjs[
                  this.service.utilities.addr_text(identity) +
                    this.service.utilities.addr_class(identity)
                ]
              ) {
                // create a new addressObjs record
                addressObjs[
                  this.service.utilities.addr_text(identity) +
                    this.service.utilities.addr_class(identity)
                ] = {
                  address: this.service.utilities.addr_text(identity),
                  class: this.service.utilities.addr_class(identity),
                  phase: addr_phase(identity),
                  distribution: address.distribution,
                  inproc: address.inProcess,
                  local: address.subscriberCount,
                  remote: address.remoteCount,
                  in: address.deliveriesIngress,
                  out: address.deliveriesEgress,
                  thru: address.deliveriesTransit,
                  toproc: address.deliveriesToContainer,
                  fromproc: address.deliveriesFromContainer,
                  identity: identity,
                  nodeId: node,
                  uid: uid
                };
              } else {
                // we've seen this address:class before. add the values
                // into the addressObjs
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
                sumObj.toproc = addNull(sumObj.toproc, address.deliveriesToContainer);
                sumObj.fromproc = addNull(
                  sumObj.fromproc,
                  address.deliveriesFromContainer
                );
              }
            });
          }
          // At this point we have created and summed all the address records.
          for (let obj in addressObjs) {
            addressFields.push(addressObjs[obj]);
          }

          // Two records that have the same address
          // are differenciated by adding a class to both records' title.
          // To do this we need to sort the array by address:class
          addressFields.sort((a, b) => {
            return a.address + a["class"] < b.address + b["class"]
              ? -1
              : a.address + a["class"] > b.address + b["class"]
              ? 1
              : 0;
          });

          // Loop through the sorted array to find records with the same address.
          // Construct a title field that has "address (class)" for records with
          // duplicate addresses, and just the address for unique records
          if (addressFields.length) {
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
                  addressFields[i].address + " (" + addressFields[i]["class"] + ")";
              } else addressFields[i].title = addressFields[i].address;
            }
          }
          resolve({ data: addressFields, page, perPage });
        }
      );
    });
  };
}

export default AddressData;
