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
/* global describe it */
var assert = require('assert');
var expect = require('chai').expect;
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
var timeout = function (f) {f();};
var qdrService = new QDRService(log, timeout, loc);

describe('Management utilities', function() {
  describe('#nameFromId', function() {
    it('should extract name from id', function() {
      let name = qdrService.utilities.nameFromId('amqp:/topo/0/routerName/$management');
      assert.equal(name, 'routerName');
    });
    it('should extract name with / from id', function() {
      let name = qdrService.utilities.nameFromId('amqp:/topo/0/router/Name/$management');
      assert.equal(name, 'router/Name');
    });
  });
  describe('#valFor', function() {
    let aAr = ['name', 'value'];
    let vAr = [['mary', 'lamb']];
    it('should return correct value for key', function() {
      let name = qdrService.utilities.valFor(aAr, vAr[0], 'name');
      assert.equal(name, 'mary');
    });
    it('should return null if key is not found', function() {
      let name = qdrService.utilities.valFor(aAr, vAr, 'address');
      assert.equal(name, null);
    });
  });
  describe('#pretty', function() {
    it('should return unchanged if not a number', function() {
      let val = qdrService.utilities.pretty('foo');
      assert.equal(val, 'foo');
    });
    it('should add commas to numbers', function() {
      let val = qdrService.utilities.pretty('1234');
      assert.equal(val, '1,234');
    });
  });
  describe('#humanify', function() {
    it('should handle empty strings', function() {
      let val = qdrService.utilities.humanify('');
      assert.equal(val, '');
    });
    it('should handle undefined input', function() {
      let val = qdrService.utilities.humanify();
      assert.equal(val, undefined);
    });
    it('should capitalize the first letter', function() {
      let val = qdrService.utilities.humanify('foo');
      assert.equal(val, 'Foo');
    });
    it('should split on all capital letters', function() {
      let val = qdrService.utilities.humanify('fooBarBaz');
      assert.equal(val, 'Foo Bar Baz');
    });
  });
  describe('#addr_class', function() {
    it('should handle unknown address types', function() {
      let val = qdrService.utilities.addr_class(' ');
      assert.equal(val, 'unknown:  ');
    });
    it('should handle undefined input', function() {
      let val = qdrService.utilities.addr_class();
      assert.equal(val, '-');
    });
    it('should identify mobile addresses', function() {
      let val = qdrService.utilities.addr_class('Mfoo');
      assert.equal(val, 'mobile');
    });
    it('should identify router addresses', function() {
      let val = qdrService.utilities.addr_class('Rfoo');
      assert.equal(val, 'router');
    });
    it('should identify area addresses', function() {
      let val = qdrService.utilities.addr_class('Afoo');
      assert.equal(val, 'area');
    });
    it('should identify local addresses', function() {
      let val = qdrService.utilities.addr_class('Lfoo');
      assert.equal(val, 'local');
    });
    it('should identify link-incoming C addresses', function() {
      let val = qdrService.utilities.addr_class('Cfoo');
      assert.equal(val, 'link-incoming');
    });
    it('should identify link-incoming E addresses', function() {
      let val = qdrService.utilities.addr_class('Efoo');
      assert.equal(val, 'link-incoming');
    });
    it('should identify link-outgoing D addresses', function() {
      let val = qdrService.utilities.addr_class('Dfoo');
      assert.equal(val, 'link-outgoing');
    });
    it('should identify link-outgoing F addresses', function() {
      let val = qdrService.utilities.addr_class('Dfoo');
      assert.equal(val, 'link-outgoing');
    });
    it('should identify topo addresses', function() {
      let val = qdrService.utilities.addr_class('Tfoo');
      assert.equal(val, 'topo');
    });
  });
  describe('#addr_text', function() {
    it('should handle undefined input', function() {
      let val = qdrService.utilities.addr_text();
      assert.equal(val, '-');
    });
    it('should identify mobile addresses', function() {
      let val = qdrService.utilities.addr_text('M0foo');
      assert.equal(val, 'foo');
    });
    it('should identify non-mobile addresses', function() {
      let val = qdrService.utilities.addr_text('Rfoo');
      assert.equal(val, 'foo');
    });
  });
  describe('#identity_clean', function() {
    it('should handle undefined input', function() {
      let val = qdrService.utilities.identity_clean();
      assert.equal(val, '-');
    });
    it('should handle identities with no /', function() {
      let val = qdrService.utilities.identity_clean('foo');
      assert.equal(val, 'foo');
    });
    it('should return everything after the 1st /', function() {
      let val = qdrService.utilities.identity_clean('foo/bar');
      assert.equal(val, 'bar');
    });
  });
  describe('#copy', function() {
    it('should handle undefined input', function() {
      let val = qdrService.utilities.copy();
      assert.equal(val, undefined);
    });
    it('should copy all object values instead making references', function() {
      let input = {a: 'original value'};
      let output = qdrService.utilities.copy(input);
      input.a = 'changed value';
      assert.equal(output.a, 'original value');
    });
  });
  describe('#flatten', function() {
    it('should return an object when passed undefined input', function() {
      let val = qdrService.utilities.flatten();
      assert.equal(typeof val, 'object');
    });
    it('and the returned object should be empty', function() {
      let val = qdrService.utilities.flatten();
      assert.equal(Object.keys(val).length, 0);
    });
    it('should flatten the arrays into an object', function() {
      let attributes = ['first', 'second'];
      let value = ['1st', '2nd'];
      let val = qdrService.utilities.flatten(attributes, value);
      assert.equal(val.second, '2nd');
    });
  });

});
