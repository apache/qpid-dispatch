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

/* global d3 ChordData MIN_CHORD_THRESHOLD */

/* Create animated dots moving along the links between routers
   to show that there is message flow between routers.
   */
const transitionDuration = 1000;
const CHORDFILTERKEY =  'chordFilter';

function Traffic ($scope, $timeout, QDRService, converter, radius, topology, nextHop, excluded) {
  this.QDRService = QDRService;
  this.radius = radius;       // the radius of a router circle
  this.topology = topology;   // contains the list of router nodes
  this.nextHop = nextHop;     // fn that returns the route through the network between two routers
  this.$scope = $scope;
  this.$timeout = $timeout;
  $scope.addressColors = {};
  this.excludedAddresses = JSON.parse(localStorage[CHORDFILTERKEY]) || [];

  // internal variables
  this.interval = null;       // setInterval handle
  this.lastFlows = {};        // the number of dots animated between routers
  this.chordData = new ChordData(this.QDRService, true, converter); // gets ingressHistogram data
  this.chordData.setFilter(this.excludedAddresses);
  this.$scope.addresses = {};
  this.chordData.getMatrix().then( function () {
    $timeout( function () {
      this.$scope.addresses = this.chordData.getAddresses();
      for (let address in this.$scope.addresses) {
        this.fillColor(address);
      }
    }.bind(this));
  }.bind(this));
}

/* Public methods on Traffic object */

// stop updating the traffic data
Traffic.prototype.stop = function () {
  if (this.interval) {
    clearInterval(this.interval);
    this.interval = null;
  }
};
// start updating the traffic data
Traffic.prototype.start = function () {
  this.doUpdate(this);
  this.interval = setInterval(this.doUpdate.bind(this), transitionDuration);
};
// remove any animationions that are in progress
Traffic.prototype.remove = function () {
  for (let id in this.lastFlows) {
    d3.select('#SVG_ID').selectAll('circle.flow' + id).remove();
  }
  this.lastFlows = {};
};
// called when one of the address checkboxes is toggled
Traffic.prototype.updateAddresses = function () {
  this.excludedAddresses = [];
  for (let address in this.$scope.addresses) {
    if (!this.$scope.addresses[address])
      this.excludedAddresses.push(address);
  }
  localStorage[CHORDFILTERKEY] = JSON.stringify(this.excludedAddresses);
  if (this.chordData) 
    this.chordData.setFilter(this.excludedAddresses);
  // don't wait for the next polling cycle. update now
  this.stop();
  this.start();
};
Traffic.prototype.toggleAddress = function (address) {
  this.$scope.addresses[address] = !this.$scope.addresses[address];
  this.updateAddresses();
};
Traffic.prototype.fadeOtherAddresses = function (address) {
  d3.selectAll('circle.flow').classed('fade', function(d) {
    return d.address !== address;
  });
};
Traffic.prototype.unFadeAll = function () {
  d3.selectAll('circle.flow').classed('fade', false);
};

/* The following don't need to be public, but they are for simplicity sake */

// called periodically to refresh the traffic flow
Traffic.prototype.doUpdate = function () {
  let self = this;
  // we need the nextHop data to show traffic between routers that are connected by intermediaries
  this.QDRService.management.topology.ensureAllEntities([{entity: 'router.node', attrs: ['id','nextHop']}], 
    function () {
      // get the ingressHistogram data for all routers
      self.chordData.getMatrix().then(self.render.bind(self), function (e) {
        console.log('Could not get message histogram' + e);
      });
    });
};

// calculate the translation for each dot along the path
let translateDots = function (radius, path, count, back) {
  let pnode = path.node();
  // will be called for each element in the flow selection (for each dot)
  return function(d) {
    // will be called with t going from 0 to 1 for each dot
    return function(t) {
      // start the points at different positions depending on their value (d)
      let tt = t * 1000;
      let f = ((tt + (d.i*1000/count)) % 1000)/1000;
      if (back)
        f = 1 - f;
      // l needs to be calculated each tick because the path's length might be changed during the animation
      let l = pnode.getTotalLength();
      let p = pnode.getPointAtLength(f * l);
      return 'translate(' + p.x + ',' + p.y + ')';
    };
  };
};
// animate the d3 selection (flow) along the given path
Traffic.prototype.animateFlow = function (flow, path, count, back, rate) {
  let self = this;
  let l = path.node().getTotalLength();
  flow.transition()
    .ease('easeLinear')
    .duration(l*10/rate)
    .attrTween('transform', translateDots(self.radius, path, count, back))
    .each('end', function () {self.animateFlow(flow, path, count, back, rate);});
};

Traffic.prototype.render = function (matrix) {
  this.$timeout(
    function () {
      this.$scope.addresses = this.chordData.getAddresses();
    }.bind(this)
  );
  // get the rate of message flow between routers
  let hops = {};  // every hop between routers that is involved in message flow
  let matrixMessages = matrix.matrixMessages();
  // the fastest traffic rate gets 3 times as many dots as the slowest
  let minmax = matrix.getMinMax();
  let flowScale = d3.scale.linear().domain(minmax).range([1,1.75]);

  // row is ingress router, col is egress router. Value at [row][col] is the rate
  matrixMessages.forEach( function (row, r) {
    row.forEach(function (val, c) {
      if (val > MIN_CHORD_THRESHOLD) {
        // translate between matrix row/col and node index
        let f = nodeIndexFor(this.topology.nodes, matrix.rows[r].egress);
        let t = nodeIndexFor(this.topology.nodes, matrix.rows[r].ingress);
        let address = matrix.getAddress(r, c);

        if (r !== c) {
          // move the dots along the links between the routers
          this.nextHop(this.topology.nodes[f], this.topology.nodes[t], function (link, fnode, tnode) {
            let key = '-' + link.uid;
            let back = fnode.index < tnode.index;
            if (!hops[key])
              hops[key] = [];
            hops[key].push({val: val, back: back, address: address});
          });
        }
        // Find the senders connected to nodes[f] and the receivers connected to nodes[t]
        // and add their links to the animation
        addClients(hops, this.topology.nodes, f, val, true, address);
        addClients(hops, this.topology.nodes, t, val, false, address);
      }
    }.bind(this));
  }.bind(this));
  // for each link between routers that has traffic, start an animation
  let keep = {};
  for (let id in hops) {
    let hop = hops[id];
    for (let h=0; h<hop.length; h++) {
      let ahop = hop[h];
      let flowId = id + '-' + addressIndex(this, ahop.address) + (ahop.back ? 'b' : '');
      let path = d3.select('#path' + id);
      // start the animation. If the animation is already running, this will have no effect
      this.startDots(path, flowId, ahop, flowScale(ahop.val));
      keep[flowId] = true;
    }
  }
  // remove any existing animations that we don't have data for anymore
  for (let id in this.lastFlows) {
    if (this.lastFlows[id] && !keep[id]) {
      this.lastFlows[id] = 0;
      d3.select('#SVG_ID').selectAll('circle.flow' + id).remove();
    }
  }
};

// create dots along the path between routers
Traffic.prototype.startDots = function (path, id, hop, rate) {
  let back = hop.back, address = hop.address;
  if (!path.node())
    return;
  // the density of dots is determined by the rate of this traffic relative to the other traffic
  let len = Math.max(Math.floor(path.node().getTotalLength() / 50), 1);
  let dots = [];
  for (let i=0, offset=addressIndex(this, address); i<len; ++i) {
    dots[i] = {i: i + 10 * offset, address: address};
  }
  // keep track of the number of dots for each link. If the length of the link is changed,
  // re-create the animation
  if (!this.lastFlows[id])
    this.lastFlows[id] = len;
  else {
    if (this.lastFlows[id] !== len) {
      this.lastFlows[id] = len;
      d3.select('#SVG_ID').selectAll('circle.flow' + id).remove();
    }
  }
  let flow = d3.select('#SVG_ID').selectAll('circle.flow' + id)
    .data(dots, function (d) { return d.i + d.address; });

  let circles = flow
    .enter().append('circle')
    .attr('class', 'flow flow' + id)
    .attr('fill', this.fillColor(address))
    .attr('r', 5);

  this.animateFlow(circles, path, dots.length, back, rate);

  flow.exit()
    .remove();
};

// colors
let colorGen = d3.scale.category10();
Traffic.prototype.fillColor = function (n) {
  if (!(n in this.$scope.addressColors)) {
    let ci = Object.keys(this.$scope.addressColors).length;
    this.$scope.addressColors[n] = colorGen(ci);
  }
  return this.$scope.addressColors[n];
};
// return the node index for a router name
let nodeIndexFor = function (nodes, name) {
  for (let i=0; i<nodes.length; i++) {
    let node = nodes[i];
    if (node.routerId === name)
      return i;
  }
  return -1;
};
let addClients = function (hops, nodes, f, val, sender, address) {
  let cdir = sender ? 'out' : 'in';
  for (let n=0; n<nodes.length; n++) {
    let node = nodes[n];
    if (node.normals && node.key === nodes[f].key && node.cdir === cdir) {
      let key = ['',f,n].join('-');
      if (!hops[key])
        hops[key] = [];
      hops[key].push({val: val, back: node.cdir === 'in', address: address});
      return;
    }
  }
};
let addressIndex = function (traffic, address) {
  return Object.keys(traffic.$scope.addresses).indexOf(address);
};