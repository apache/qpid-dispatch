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

import {utils} from './utilities';

describe('Management utilities', function() {
  describe('#nameFromId', function() {
    it('should extract name from id', function() {
      let name = utils.nameFromId('amqp:/topo/0/routerName/$management');
      expect(name).toEqual('routerName');
    });
    it('should extract name with / from id', function() {
      let name = utils.nameFromId('amqp:/topo/0/router/Name/$management');
      expect(name).toEqual('router/Name');
    });
    it('should extract from edge router id', function() {
      let name = utils.nameFromId('amqp:/_edge/edgeName/$management');
      expect(name).toEqual('edgeName');
    });
    it('should extract name with / from edge router id', function() {
      let name = utils.nameFromId('amqp:/_edge/edge/Name/$management');
      expect(name).toEqual('edge/Name');
    });
    it('should extract name with multiple /s from router id', function() {
      let name = utils.nameFromId('amqp:/_topo/0/router/Name/here/$management');
      expect(name).toEqual('router/Name/here');
    });
    it('should extract name with multiple /s from edge router id', function() {
      let name = utils.nameFromId('amqp:/_edge/edge/Name/here/$management');
      expect(name).toEqual('edge/Name/here');
    });
  });
  describe('#valFor', function() {
    let aAr = ['name', 'value'];
    let vAr = [['mary', 'lamb']];
    it('should return correct value for key', function() {
      let name = utils.valFor(aAr, vAr[0], 'name');
      expect(name).toEqual('mary');
    });
    it('should return null if key is not found', function() {
      let name = utils.valFor(aAr, vAr, 'address');
      expect(name).toEqual(null);
    });
  });
  describe('#pretty', function() {
    it('should return unchanged if not a number', function() {
      let val = utils.pretty('foo');
      expect(val).toEqual('foo');
    });
    it('should add commas to numbers', function() {
      let val = utils.pretty('1234');
      expect(val).toEqual('1,234');
    });
  });
  describe('#humanify', function() {
    it('should handle empty strings', function() {
      let val = utils.humanify('');
      expect(val).toEqual('');
    });
    it('should handle undefined input', function() {
      let val = utils.humanify();
      expect(val).toEqual(undefined);
    });
    it('should capitalize the first letter', function() {
      let val = utils.humanify('foo');
      expect(val).toEqual('Foo');
    });
    it('should split on all capital letters', function() {
      let val = utils.humanify('fooBarBaz');
      expect(val).toEqual('Foo Bar Baz');
    });
  });
  describe('#addr_class', function() {
    it('should handle unknown address types', function() {
      let val = utils.addr_class(' ');
      expect(val).toEqual('unknown:  ');
    });
    it('should handle undefined input', function() {
      let val = utils.addr_class();
      expect(val).toEqual('-');
    });
    it('should identify mobile addresses', function() {
      let val = utils.addr_class('Mfoo');
      expect(val).toEqual('mobile');
    });
    it('should identify router addresses', function() {
      let val = utils.addr_class('Rfoo');
      expect(val).toEqual('router');
    });
    it('should identify area addresses', function() {
      let val = utils.addr_class('Afoo');
      expect(val).toEqual('area');
    });
    it('should identify local addresses', function() {
      let val = utils.addr_class('Lfoo');
      expect(val).toEqual('local');
    });
    it('should identify link-incoming C addresses', function() {
      let val = utils.addr_class('Cfoo');
      expect(val).toEqual('link-incoming');
    });
    it('should identify link-incoming E addresses', function() {
      let val = utils.addr_class('Efoo');
      expect(val).toEqual('link-incoming');
    });
    it('should identify link-outgoing D addresses', function() {
      let val = utils.addr_class('Dfoo');
      expect(val).toEqual('link-outgoing');
    });
    it('should identify link-outgoing F addresses', function() {
      let val = utils.addr_class('Dfoo');
      expect(val).toEqual('link-outgoing');
    });
    it('should identify topo addresses', function() {
      let val = utils.addr_class('Tfoo');
      expect(val).toEqual('topo');
    });
  });
  describe('#addr_text', function() {
    it('should handle undefined input', function() {
      let val = utils.addr_text();
      expect(val).toEqual('-');
    });
    it('should identify mobile addresses', function() {
      let val = utils.addr_text('M0foo');
      expect(val).toEqual('foo');
    });
    it('should identify non-mobile addresses', function() {
      let val = utils.addr_text('Rfoo');
      expect(val).toEqual('foo');
    });
  });
  describe('#identity_clean', function() {
    it('should handle undefined input', function() {
      let val = utils.identity_clean();
      expect(val).toEqual('-');
    });
    it('should handle identities with no /', function() {
      let val = utils.identity_clean('foo');
      expect(val).toEqual('foo');
    });
    it('should return everything after the 1st /', function() {
      let val = utils.identity_clean('foo/bar');
      expect(val).toEqual('bar');
    });
  });
  describe('#copy', function() {
    it('should handle undefined input', function() {
      let val = utils.copy();
      expect(val).toEqual(undefined);
    });
    it('should copy all object values instead making references', function() {
      let input = {a: 'original value'};
      let output = utils.copy(input);
      input.a = 'changed value';
      expect(output.a).toEqual('original value');
    });
  });
  describe('#flatten', function() {
    it('should return an object when passed undefined input', function() {
      let val = utils.flatten();
      expect(typeof val).toEqual('object');
    });
    it('and the returned object should be empty', function() {
      let val = utils.flatten();
      expect(Object.keys(val).length).toEqual(0);
    });
    it('should flatten the arrays into an object', function() {
      let attributes = ['first', 'second'];
      let value = ['1st', '2nd'];
      let val = utils.flatten(attributes, value);
      expect(val.second).toEqual('2nd');
    });
  });
  describe('#flattenAll', function() {
    let vals = [];
    let attributes = ['first', 'second'];
    let values = [['1st', '2nd'], ['3rd', '4th']];
    it('should flatten the router entity into an array of objects', function() {
      vals = utils.flattenAll({attributeNames: attributes, results: values});
      expect(vals.length).toEqual(2);
    });
    it('should correctly create the objects', function() {
      expect(vals[1].second).toEqual('4th');
    });
    it('should filter out objects', function() {
      vals = utils.flattenAll({attributeNames: attributes, results: values}, function (f) {
        return f.first === '1st' ? {first: 'first', second: 'second'} : null;
      });
      expect(vals.length).toEqual(1);
      expect(vals[0].second).toEqual('second');
    });
  });

});
