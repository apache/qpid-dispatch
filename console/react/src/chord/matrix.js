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

import * as d3 from "d3";
const MIN_CHORD_THRESHOLD = 0.01;

// public Matrix object
function valuesMatrix(aggregate) {
  this.rows = [];
  this.aggregate = aggregate;
}
// a matrix row
let valuesMatrixRow = function(r, chordName, ingress, egress) {
  this.chordName = chordName || "";
  this.ingress = ingress || "";
  this.egress = egress || "";
  this.index = r;
  this.cols = [];
  for (let c = 0; c < r; c++) {
    this.addCol(0);
  }
};
// a matrix column
let valuesMatrixCol = function(messages, row, c, address) {
  this.messages = messages;
  this.address = address;
  this.index = c;
  this.row = row;
};

// initialize a matrix with empty data with size rows and columns
valuesMatrix.prototype.zeroInit = function(size) {
  for (let r = 0; r < size; r++) {
    this.addRow();
  }
};

valuesMatrix.prototype.setRowCol = function(r, c, ingress, egress, address, value) {
  this.rows[r].ingress = ingress;
  this.rows[r].egress = egress;
  this.rows[r].cols[c].messages = value;
  this.rows[r].cols[c].address = address;
};

valuesMatrix.prototype.setColMessages = function(r, c, messages) {
  this.rows[r].cols[c].messages = messages;
};

// return true if any of the matrix cells have messages
valuesMatrix.prototype.hasValues = function() {
  return this.rows.some(function(row) {
    return row.cols.some(function(col) {
      return col.messages > MIN_CHORD_THRESHOLD;
    });
  });
};
valuesMatrix.prototype.getMinMax = function() {
  let min = Number.MAX_VALUE,
    max = Number.MIN_VALUE;
  this.rows.forEach(function(row) {
    row.cols.forEach(function(col) {
      if (col.messages > MIN_CHORD_THRESHOLD) {
        max = Math.max(max, col.messages);
        min = Math.min(min, col.messages);
      }
    });
  });
  return [min, max];
};
// extract a square matrix with just the values from the object matrix
valuesMatrix.prototype.matrixMessages = function() {
  let m = emptyMatrix(this.rows.length);
  this.rows.forEach(function(row, r) {
    row.cols.forEach(function(col, c) {
      m[r][c] = col.messages;
    });
  });
  return m;
};

valuesMatrix.prototype.getGroupBy = function() {
  if (!this.aggregate && this.rows.length) {
    let groups = [];
    let lastName = this.rows[0].egress,
      groupIndex = 0;
    this.rows.forEach(function(row) {
      if (row.egress !== lastName) {
        groupIndex++;
        lastName = row.egress;
      }
      groups.push(groupIndex);
    });
    return groups;
  } else return d3.range(this.rows.length);
};

valuesMatrix.prototype.chordName = function(i, ingress) {
  if (this.aggregate) return this.rows[i].chordName;
  return ingress ? this.rows[i].ingress : this.rows[i].egress;
};
valuesMatrix.prototype.routerName = function(i) {
  if (this.aggregate) return this.rows[i].chordName;
  return getAttribute(this, "egress", i);
};
valuesMatrix.prototype.getEgress = function(i) {
  return getAttribute(this, "egress", i);
};
valuesMatrix.prototype.getIngress = function(i) {
  return getAttribute(this, "ingress", i);
};
valuesMatrix.prototype.getAddress = function(r, c) {
  return this.rows[r].cols[c].address;
};
valuesMatrix.prototype.getAddresses = function(r) {
  let addresses = {};
  this.rows[r].cols.forEach(function(c) {
    if (c.address && c.messages) addresses[c.address] = true;
  });
  return Object.keys(addresses);
};
let getAttribute = function(self, attr, i) {
  if (self.aggregate) return self.rows[i][attr];
  let groupByIndex = self.getGroupBy().indexOf(i);
  if (groupByIndex < 0) {
    groupByIndex = i;
  }
  return self.rows[groupByIndex][attr];
};
valuesMatrix.prototype.addRow = function(chordName, ingress, egress, address) {
  let rowIndex = this.rows.length;
  let newRow = new valuesMatrixRow(rowIndex, chordName, ingress, egress);
  this.rows.push(newRow);
  // add new column to all rows
  for (let r = 0; r <= rowIndex; r++) {
    this.rows[r].addCol(0, address);
  }
  return rowIndex;
};
valuesMatrix.prototype.indexOf = function(chordName) {
  return this.rows.findIndex(function(row) {
    return row.chordName === chordName;
  });
};
valuesMatrix.prototype.addValue = function(r, c, value) {
  this.rows[r].cols[c].addMessages(value.messages);
  this.rows[r].cols[c].setAddress(value.address);
};
valuesMatrixRow.prototype.addCol = function(messages, address) {
  this.cols.push(new valuesMatrixCol(messages, this, this.cols.length, address));
};
valuesMatrixCol.prototype.addMessages = function(messages) {
  if (!(this.messages === MIN_CHORD_THRESHOLD && messages === MIN_CHORD_THRESHOLD))
    this.messages += messages;
};
valuesMatrixCol.prototype.setAddress = function(address) {
  this.address = address;
};
valuesMatrix.prototype.getChordList = function() {
  return this.rows.map(function(row) {
    return row.chordName;
  });
};
valuesMatrix.prototype.sorted = function() {
  let newChordList = this.getChordList();
  newChordList.sort();
  let m = new valuesMatrix(this.aggregate);
  m.zeroInit(this.rows.length);
  this.rows.forEach(
    function(row) {
      let chordName = row.chordName;
      row.cols.forEach(
        function(col, c) {
          let newRow = newChordList.indexOf(chordName);
          let newCol = newChordList.indexOf(this.rows[c].chordName);
          m.rows[newRow].chordName = chordName;
          m.rows[newRow].ingress = row.ingress;
          m.rows[newRow].egress = row.egress;
          m.rows[newRow].cols[newCol].messages = col.messages;
          m.rows[newRow].cols[newCol].address = col.address;
        }.bind(this)
      );
    }.bind(this)
  );
  return m;
};

// private helper function
let emptyMatrix = function(size) {
  let matrix = [];
  for (let i = 0; i < size; i++) {
    matrix[i] = [];
    for (let j = 0; j < size; j++) {
      matrix[i][j] = 0;
    }
  }
  return matrix;
};

export { MIN_CHORD_THRESHOLD, valuesMatrix };
