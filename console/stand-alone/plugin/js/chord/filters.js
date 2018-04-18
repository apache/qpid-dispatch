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
'use strict';
/* global valuesMatrix */

// this filter will show an arc per router with the addresses aggregated
var aggregateAddresses = function (values, filter) { // eslint-disable-line no-unused-vars
  let m = new valuesMatrix(true);
  values.forEach (function (value) {
    if (filter.indexOf(value.address) < 0) {
      let chordName = value.egress;
      let egress = value.ingress;
      let row = m.indexOf(chordName);
      if (row < 0) {
        row = m.addRow(chordName, value.ingress, value.egress, value.address);
      }
      let col = m.indexOf(egress);
      if (col < 0) {
        col = m.addRow(egress, value.ingress, value.egress, value.address);
      }
      m.addValue(row, col, value);
    }
  });
  return m.sorted();
};

// this filter will show an arc per router-address
var _separateAddresses = function (values, filter) { // eslint-disable-line no-unused-vars
  let m = new valuesMatrix(false);
  values = values.filter( function (v) { return filter.indexOf(v.address) < 0;});
  if (values.length === 0)
    return m;

  let addresses = {}, routers = {};
  // get the list of routers and addresses in the data
  values.forEach( function (value) {
    addresses[value.address] = true;
    routers[value.ingress] = true;
    routers[value.egress] = true;
  });
  let saddresses = Object.keys(addresses).sort();
  let srouters = Object.keys(routers).sort();
  let alen = saddresses.length;
  // sanity check
  if (alen === 0)
    return m;

  /* Convert the data to a matrix */

  // initialize the matrix to have the correct ingress, egress, and address in each row and col
  m.zeroInit(saddresses.length * srouters.length);
  m.rows.forEach( function (row, r) {
    let egress = srouters[Math.floor(r/alen)];
    row.cols.forEach( function (col, c) {
      let ingress = srouters[Math.floor(c/alen)];
      let address = saddresses[c % alen];
      m.setRowCol(r, c, ingress, egress, address, 0);
    });
  });
  // set the values at each cell in the matrix
  for (let i=0, alen=saddresses.length, vlen=values.length; i<vlen; i++) {
    let value = values[i];
    let egressIndex = srouters.indexOf(value.egress);
    let ingressIndex = srouters.indexOf(value.ingress);
    let addressIndex = saddresses.indexOf(value.address);
    let row = egressIndex * alen + addressIndex;
    let col = ingressIndex * alen + addressIndex;
    m.setColMessages(row, col, value.messages);
  }
  return m;
};

let separateAddresses = function (values, filter) { // eslint-disable-line no-unused-vars
  let m = new valuesMatrix(false);
  values.forEach( function (value) {
    if (filter.indexOf(value.address) < 0) {
      let egressChordName = value.egress + value.ingress + value.address;
      let r = m.indexOf(egressChordName);
      if (r < 0) {
        r = m.addRow(egressChordName, value.ingress, value.egress, value.address);
      }
      let ingressChordName = value.ingress + value.egress + value.address;
      let c = m.indexOf(ingressChordName);
      if (c < 0) {
        c = m.addRow(ingressChordName, value.egress, value.ingress, value.address);
      }
      m.addValue(r, c, value);
    }
  });
  return m.sorted();
};
