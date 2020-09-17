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

import {valuesMatrix} from './matrix.js';

describe('Matrix', function() {
  describe('#zeroInit', function() {
    const ROW_COUNT = 10;
    let matrix = new valuesMatrix(false);
    matrix.zeroInit(ROW_COUNT);
    it('should create the requested number of rows', function() {
      expect(matrix.rows.length).toEqual(ROW_COUNT);
    });
    it('should create the requested number of cols per row', function() {
      matrix.rows.forEach( function (row) {
        expect(row.cols.length).toEqual(ROW_COUNT);
      });
    });
  });
  describe('#hasValues', function () {
    it('should not have any values to start', function () {
      let matrix = new valuesMatrix(false);
      expect(matrix.hasValues()).toBeFalsy();
    });
    it('should have a value after adding one', function () {
      let matrix = new valuesMatrix(false);
      matrix.addRow('chordName', 'ingress', 'egress', 'address');
      matrix.addValue(0, 0, {messages: 1234, address: 'address'});
      expect(matrix.hasValues()).toBeTruthy();
    });
  });
});
