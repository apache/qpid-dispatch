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
/* global describe it before */
var assert = require('assert');
var fs = require('fs');
var expect = require('chai').expect;
import { Links } from '../plugin/js/topology/links.js';
import { Nodes } from '../plugin/js/topology/nodes.js';

class Log {
  constructor() {
  }
  log(msg) { console.log(msg); }
  debug(msg) { console.log(`Debug: ${msg}`); }
  error(msg) { console.log(`Error: ${msg}`); }
  info(msg) { console.log(`Info: ${msg}`); }
  warn(msg) { console.log(`Warning: ${msg}`); }
}
var log = new Log();
var links = new Links(log);
var edgeLinks = new Links(log);
var nodes = new Nodes(log);
var edgeNodes = new Nodes(log);
var nodeInfo;
var edgeInfo;
var unknowns = [];
const width = 1024;
const height = 768;


before(function (done) {
  let src = '';
  let LAST_PARAM = process.argv[process.argv.length - 1];

  let PARAM_NAME = LAST_PARAM.split('=')[0].replace('--', '');
  let PARAM_VALUE = LAST_PARAM.split('=')[1];
  if (PARAM_NAME == 'src') {
    src = PARAM_VALUE;
  }

  fs.readFile(src + './test/nodes.json', 'utf8', function (err, fileContents) {
    if (err) throw err;
    nodeInfo = JSON.parse(fileContents);
  });
  fs.readFile(src + './test/nodes-edge.json', 'utf8', function (err, fileContents) {
    if (err) throw err;
    edgeInfo = JSON.parse(fileContents);
    done();
  });
});

describe('Nodes', function () {
  describe('#exists', function () {
    it('should exist', function () {
      expect(nodes).to.exist;
    });
  });
  describe('#initializes', function () {
    it('should initialize', function () {
      nodes.initialize(nodeInfo, {}, width, height, {});
      assert.equal(nodes.nodes.length, 6);
    });
    it('should initialize edge nodes', function () {
      edgeNodes.initialize(edgeInfo, {}, width, height, {});
      assert.equal(edgeNodes.nodes.length, 2);
    });
  });

});
describe('Links', function () {
  describe('#exists', function () {
    it('should exist', function () {
      expect(links).to.exist;
    });
  });
  describe('#initializes', function () {
    it('should initialize', function () {
      links.initialize(nodeInfo, nodes, unknowns, {}, width);
      assert.equal(links.links.length, 10);
    });
    it('should initialize edge links', function () {
      edgeLinks.initialize(edgeInfo, edgeNodes, unknowns, {}, width);
      assert.equal(edgeLinks.links.length, 6);
    });
    it('should add nodes for edge router groups', function () {
      assert.equal(edgeNodes.nodes.length, 6);
    });
  });

});
