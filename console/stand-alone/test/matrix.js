var assert = require('assert');
import { valuesMatrix } from '../plugin/js/chord/matrix.js';
/* global describe it */

describe('Matrix', function() {
  describe('#zeroInit', function() {
    const ROW_COUNT = 10;
    let matrix = new valuesMatrix(false);
    matrix.zeroInit(ROW_COUNT);
    it('should create the requested number of rows', function() {
      assert.equal(matrix.rows.length, ROW_COUNT);
    });
    it('should create the requested number of cols per row', function() {
      let allEqual = true;
      matrix.rows.forEach( function (row) {
        allEqual = allEqual && row.cols.length === ROW_COUNT;
      });
      assert.ok(allEqual);
    });
  });
  describe('#hasValues', function () {
    it('should not have any values to start', function () {
      let matrix = new valuesMatrix(false);
      assert.ok(!matrix.hasValues());
    });
    it('should have a value after adding one', function () {
      let matrix = new valuesMatrix(false);
      matrix.addRow('chordName', 'ingress', 'egress', 'address');
      matrix.addValue(0, 0, {messages: 1234, address: 'address'});
      assert.ok(matrix.hasValues());
    });
  });
});
