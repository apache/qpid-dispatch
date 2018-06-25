/* global describe it before fs */
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
  fs.readFile('./test/nodes.json', 'utf8', function(err, fileContents) {
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
