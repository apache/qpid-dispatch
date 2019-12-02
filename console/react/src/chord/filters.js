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

import { valuesMatrix } from './matrix.js';
// this filter will show an arc per router with the addresses aggregated
export const aggregateAddresses = function (values, filter) {
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


export const separateAddresses = function (values, filter) {
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
