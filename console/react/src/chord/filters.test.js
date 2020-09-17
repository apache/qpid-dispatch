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

import {aggregateAddresses, separateAddresses} from './filters.js';

describe('Filters', function() {
  const MIN_VALUE = 1,
    MAX_VALUE = 2;
  const values = [
    {ingress: 'A', egress: 'B', address: 'toB', messages: MIN_VALUE, key: 'BAtoB'},
    {ingress: 'B', egress: 'A', address: 'toA', messages: MAX_VALUE, key: 'ABtoA'}
  ];

  describe('#aggregateAddresses', function() {
    let m = aggregateAddresses(values, []);
    it('should create a matrix', function() {
      expect(m.hasValues()).toBeTruthy();
    });
    it('that has two rows', function() {
      expect(m.rows.length).toEqual(2);
    });
    it('and has 1 chord per router', function() {
      expect(m.getChordList().length).toEqual(2);
    });
    it('and contains all addresses when there is no filter', function () {
      let minmax = m.getMinMax();
      expect(minmax[0]).toEqual(MIN_VALUE);
      expect(minmax[1]).toEqual(MAX_VALUE);
    });
    it('should filter out an address', function () {
      let m = aggregateAddresses(values, ['toB']);
      // if the toB address was filtered, the min value in the matrix should be 2 (for the toA address)
      expect(m.getMinMax()[0]).toEqual(MAX_VALUE);
    });
  });
  describe('#separateAddresses', function() {
    let m = separateAddresses(values, []);
    it('should create a matrix', function() {
      expect(m.hasValues()).toBeTruthy();
    });
    it('that has a row per router/address combination', function() {
      expect(m.rows.length).toEqual(4);
    });
    it('and has 1 chord per router/address combination', function() {
      expect(m.getChordList().length).toEqual(4);
    });
    it('and contains all addresses when there is no filter', function () {
      let minmax = m.getMinMax();
      expect(minmax[0]).toEqual(MIN_VALUE);
      expect(minmax[1]).toEqual(MAX_VALUE);
    });
    it('should filter out an address', function () {
      let m = separateAddresses(values, ['toB']);
      // if the toB address was filtered, the min value in the matrix should be 2 (for the toA address)
      expect(m.getMinMax()[0]).toEqual(MAX_VALUE);
    });
  });
});
