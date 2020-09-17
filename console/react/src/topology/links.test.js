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

import {default as fs} from 'fs';
import {Links} from './links.js';
import {Nodes} from './nodes.js';

class Log {
  log(msg) { console.log(msg); }
  debug(msg) { console.log(`Debug: ${msg}`); }
  error(msg) { console.log(`Error: ${msg}`); }
  info(msg) { console.log(`Info: ${msg}`); }
  warn(msg) { console.log(`Warning: ${msg}`); }
}

const log = new Log();
const links = new Links(log);
const edgeLinks = new Links(log);
const nodes = new Nodes(log);
const edgeNodes = new Nodes(log);
const width = 1024;
const height = 768;

let nodeInfo;
let edgeInfo;

beforeEach(function (done) {
  let src = '';
  let LAST_PARAM = process.argv[process.argv.length - 1];

  let PARAM_NAME = LAST_PARAM.split('=')[0].replace('--', '');
  let PARAM_VALUE = LAST_PARAM.split('=')[1];
  if (PARAM_NAME === 'src') {
    src = PARAM_VALUE;
  }

  fs.readFile(src + 'test_data/nodes.json', 'utf8', function (err, fileContents) {
    if (err) throw err;
    nodeInfo = JSON.parse(fileContents);
  });
  fs.readFile(src + 'test_data/nodes-edge.json', 'utf8', function (err, fileContents) {
    if (err) throw err;
    edgeInfo = JSON.parse(fileContents);
    done();
  });
});

describe('Nodes', function () {
  describe('#exists', function () {
    it('should exist', function () {
      expect(nodes).toBeDefined();
    });
  });
  describe('#initializes', function () {
    it('should initialize', function () {
      nodes.initialize(nodeInfo, width, height, {});
      expect(nodes.nodes.length).toEqual(6);
    });
    it('should initialize edge nodes', function () {
      edgeNodes.initialize(edgeInfo, width, height, {});
      expect(edgeNodes.nodes.length).toEqual(2);
    });
  });

});
describe('Links', function () {
  describe('#exists', function () {
    it('should exist', function () {
      expect(links).toBeDefined();
    });
  });
  describe('#initializes', function () {
    const separateContainers = new Set();
    const unknowns = [];
    const localStorage = {};
    it('should initialize', function () {
      links.initialize(nodeInfo, nodes, separateContainers, unknowns, height, localStorage);
      expect(links.links.length).toEqual(10);
    });
    it('should initialize edge links', function () {
      edgeLinks.initialize(edgeInfo, edgeNodes, separateContainers, unknowns, height, localStorage);
      expect(edgeLinks.links.length).toEqual(6);
    });
    it('should add nodes for edge router groups', function () {
      expect(edgeNodes.nodes.length).toEqual(6);
    });
  });

});
