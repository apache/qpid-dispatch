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
import * as d3path from "d3-path";
const halfPI = Math.PI / 2.0;
const twoPI = Math.PI * 2.0;

// These are scales to interpolate how the bezier control point should be adjusted.
// These numbers were determined emperically by adjusting a chord and discovering
// the relationship between the width of the inner bezier and the lengths of the arcs.
// If we were just drawing the chord diagram once, we wouldn't need to use scales.
// But since we are animating chords, we need to smoothly chnage the control point from
// [0, 0] to 1/2 way to the center of the bezier curve.
const dom = [0.06, 0.98, Math.PI];
const ys = d3.scale
  .linear()
  .domain(dom)
  .range([0.18, 0, 0]);
const x0s = d3.scale
  .linear()
  .domain(dom)
  .range([0.03, 0.24, 0.24]);
const x1s = d3.scale
  .linear()
  .domain(dom)
  .range([0.24, 0.6, 0.6]);
const x2s = d3.scale
  .linear()
  .domain(dom)
  .range([1.32, 0.8, 0.8]);
const x3s = d3.scale
  .linear()
  .domain(dom)
  .range([3, 2, 2]);

function qdrRibbon() {
  // eslint-disable-line no-unused-vars
  var r = 200; // random default. this will be set later

  // This is the function that gets called to produce a path for a chord.
  // The path should end up looking like
  // M[start point]A[arc options][arc end point]Q[control point][end points]A[arc options][arc end point]Q[control point][end points]Z
  var ribbon = function(d) {
    let sa0 = d.source.startAngle - halfPI,
      sa1 = d.source.endAngle - halfPI,
      ta0 = d.target.startAngle - halfPI,
      ta1 = d.target.endAngle - halfPI;

    // The control points for the bezier curves
    let cp1 = [0, 0];
    let cp2 = [0, 0];
    // the span of the two arcs
    let arc1 = Math.abs(sa0 - sa1);
    let arc2 = Math.abs(ta0 - ta1);
    let largeArc = Math.max(arc1, arc2);
    let smallArc = Math.min(arc1, arc2);
    // the gaps between the arcs
    let gap1 = Math.abs(sa1 - ta0);
    if (gap1 > Math.PI) gap1 = twoPI - gap1;
    let gap2 = Math.abs(sa0 - ta1);
    if (gap2 > Math.PI) gap2 = twoPI - gap2;
    let sgap = Math.min(gap1, gap2);

    // if the bezier curves intersect, ratiocp will be > 0
    let ratiocp = cpRatio(sgap, largeArc, smallArc);

    // x, y points for the start and end of the arcs
    let s0x = r * Math.cos(sa0),
      s0y = r * Math.sin(sa0),
      t0x = r * Math.cos(ta0),
      t0y = r * Math.sin(ta0);

    if (ratiocp > 0) {
      // determine which control point to calculate
      if (Math.abs(gap1 - gap2) < 1e-2 || gap1 < gap2) {
        let s1x = r * Math.cos(sa1),
          s1y = r * Math.sin(sa1);
        cp1 = [(ratiocp * (s1x + t0x)) / 2, (ratiocp * (s1y + t0y)) / 2];
      } else {
        let t1x = r * Math.cos(ta1),
          t1y = r * Math.sin(ta1);
        cp2 = [(ratiocp * (t1x + s0x)) / 2, (ratiocp * (t1y + s0y)) / 2];
      }
    }

    // construct the path using the control points
    let path = d3path.path();
    path.moveTo(s0x, s0y);
    path.arc(0, 0, r, sa0, sa1);
    if (sa0 !== ta0 || sa1 !== ta1) {
      path.quadraticCurveTo(cp1[0], cp1[1], t0x, t0y);
      path.arc(0, 0, r, ta0, ta1);
    }
    path.quadraticCurveTo(cp2[0], cp2[1], s0x, s0y);
    path.closePath();
    return path + "";
  };
  ribbon.radius = function(radius) {
    if (!arguments.length) return r;
    r = radius;
    return ribbon;
  };
  return ribbon;
}

let sqr = function(n) {
  return n * n;
};
let dist = function(p1x, p1y, p2x, p2y) {
  return sqr(p1x - p2x) + sqr(p1y - p2y);
};
// distance from a point to a line segment
let distToLine = function(vx, vy, wx, wy, px, py) {
  let vlen = dist(vx, vy, wx, wy);
  if (vlen === 0) return dist(px, py, vx, vy);
  var t = ((px - vx) * (wx - vx) + (py - vy) * (wy - vy)) / vlen;
  t = Math.max(0, Math.min(1, t)); // clamp t to between 0 and 1
  return Math.sqrt(dist(px, py, vx + t * (wx - vx), vy + t * (wy - vy)));
};

// See if x, y is contained in trapezoid.
// gap is the smallest gap in the chord
// x is the size of the longest arc
// y is the size of the smallest arc
// the trapezoid is defined by [x0, 0] [x1, top] [x2, top] [x3, 0]
// these points are determined by the gap
let cpRatio = function(gap, x, y) {
  let top = ys(gap);
  if (y >= top) return 0;

  // get the xpoints of the trapezoid
  let x0 = x0s(gap);
  if (x <= x0) return 0;
  let x3 = x3s(gap);
  if (x > x3) return 0;

  let x1 = x1s(gap);
  let x2 = x2s(gap);

  // see if the point is to the right of (inside) the leftmost diagonal
  // compute the outer product of the left diagonal and the point
  let op = (x - x0) * top - y * (x1 - x0);
  if (op <= 0) return 0;
  // see if the point is to the left of the right diagonal
  op = (x - x3) * top - y * (x2 - x3);
  if (op >= 0) return 0;

  // the point is in the trapezoid. see how far in
  let dist = 0;
  if (x < x1) {
    // left side. get distance to left diagonal
    dist = distToLine(x0, 0, x1, top, x, y);
  } else if (x > x2) {
    // right side. get distance to right diagonal
    dist = distToLine(x3, 0, x2, top, x, y);
  } else {
    // middle. get distance to top
    dist = top - y;
  }
  let distScale = d3.scale
    .linear()
    .domain([0, top / 8, top / 2, top])
    .range([0, 0.3, 0.4, 0.5]);
  return distScale(dist);
};

export { qdrRibbon };
