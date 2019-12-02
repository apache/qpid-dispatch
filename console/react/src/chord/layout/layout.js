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

var qdrlayoutChord = function() {
  // eslint-disable-line no-unused-vars
  var chord = {},
    chords,
    groups,
    matrix,
    n,
    padding = 0,
    τ = Math.PI * 2,
    groupBy;
  function relayout() {
    groupBy = groupBy || d3.range(n);
    // number of unique values in the groupBy array. This will be the number
    // of groups generated.
    var groupLen = unique(groupBy);
    var subgroups = {},
      groupSums = fill(0, groupLen),
      k,
      x,
      x0,
      i,
      j,
      di,
      ldi;

    chords = [];
    groups = [];

    // calculate the sum of the values for each group
    k = 0;
    i = -1;
    while (++i < n) {
      x = 0;
      j = -1;
      while (++j < n) {
        x += matrix[i][j];
      }
      groupSums[groupBy[i]] += x;
      k += x;
    }
    // the fraction of the circle for each incremental value
    k = (τ - padding * groupLen) / k;
    // for each row
    x = 0;
    i = -1;
    ldi = groupBy[0];
    while (++i < n) {
      di = groupBy[i];
      // insert padding after each group
      if (di !== ldi) {
        x += padding;
        ldi = di;
      }
      // for each column
      x0 = x;
      j = -1;
      while (++j < n) {
        var dj = groupBy[j],
          v = matrix[i][j],
          a0 = x,
          a1 = (x += v * k);
        // create a structure for each cell in the matrix. these are the potential chord ends
        subgroups[i + "-" + j] = {
          index: di,
          subindex: dj,
          orgindex: i,
          orgsubindex: j,
          startAngle: a0,
          endAngle: a1,
          value: v
        };
      }
      if (!groups[di]) {
        // create a new group (arc)
        groups[di] = {
          index: di,
          startAngle: x0,
          endAngle: x,
          value: groupSums[di]
        };
      } else {
        // bump up the ending angle of the combined arc
        groups[di].endAngle = x;
      }
    }

    // put the chord ends together into a chords.
    i = -1;
    while (++i < n) {
      j = i - 1;
      while (++j < n) {
        var source = subgroups[i + "-" + j],
          target = subgroups[j + "-" + i];
        // Only make a chord if there is a value at one of the two ends
        if (source.value || target.value) {
          chords.push(
            source.value < target.value
              ? {
                  source: target,
                  target: source
                }
              : {
                  source: source,
                  target: target
                }
          );
        }
      }
    }
  }
  chord.matrix = function(x) {
    if (!arguments.length) return matrix;
    n = (matrix = x) && matrix.length;
    chords = groups = null;
    return chord;
  };
  chord.padding = function(x) {
    if (!arguments.length) return padding;
    padding = x;
    chords = groups = null;
    return chord;
  };
  chord.groupBy = function(x) {
    if (!arguments.length) return groupBy;
    groupBy = x;
    chords = groups = null;
    return chord;
  };
  chord.chords = function() {
    if (!chords) relayout();
    return chords;
  };
  chord.groups = function() {
    if (!groups) relayout();
    return groups;
  };
  return chord;
};

let fill = function(value, length) {
  var i = 0,
    array = [];
  array.length = length;
  while (i < length) array[i++] = value;
  return array;
};

let unique = function(arr) {
  var counts = {};
  for (var i = 0; i < arr.length; i++) {
    counts[arr[i]] = 1 + (counts[arr[i]] || 0);
  }
  return Object.keys(counts).length;
};

export { qdrlayoutChord };
