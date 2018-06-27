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
import { QDRService } from '../plugin/js/qdrService.js';

class Log {
  constructor() {
  }
  log (msg) {}
  debug (msg) {}
  error (msg) {}
  info (msg) {}
  warn (msg) {}
}
var log = new Log();
var loc = {protocol: function () { return 'http://';}};
var timeout = {};
var qdrService = new QDRService(log, timeout, loc);
var links = new Links(qdrService, log);
var nodes = new Nodes(qdrService, log);
var nodeInfo;
var unknowns = [];
const width = 1024;
const height = 768;


before(function(done){
  let src = '';
  let LAST_PARAM = process.argv[process.argv.length-1];

  let PARAM_NAME  = LAST_PARAM.split('=')[0].replace('--','');
  let PARAM_VALUE = LAST_PARAM.split('=')[1];
  if (PARAM_NAME == 'src') {
    src = PARAM_VALUE;
  }
  
  console.log('src: ', src);
  fs.readFile(src + './test/nodes.json', 'utf8', function(err, fileContents) {
    if (err) throw err;
    nodeInfo = JSON.parse(fileContents);
    done();
  });
});

describe('Nodes', function() {
  describe('#exists', function() {
    it('should exist', function() {
      expect(nodes).to.exist;
    });
  });
  describe('#initializes', function() {
    it('should initialize', function() {
      nodes.initialize(nodeInfo, {}, width, height);
      assert.equal(nodes.nodes.length, 6);
    });
  });

});
describe('Links', function() {
  describe('#exists', function() {
    it('should exist', function() {
      expect(links).to.exist;
    });
  });
  describe('#initializes', function() {
    it('should initialize', function() {
      links.initializeLinks(nodeInfo, nodes, unknowns, {}, width);
      assert.equal(links.links.length, 10);
    });
  });

});
