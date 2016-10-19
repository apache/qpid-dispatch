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

(function() {
    console.dump = function(object) {
        if (window.JSON && window.JSON.stringify)
            QDR.log.info(JSON.stringify(object,undefined,2));
        else
            console.log(object);
    };
})();

var QDR = (function(QDR) {
  'use strict';

  // The QDR service handles the connection to
  // the server in the background
  angular
    .module('horizon.dashboard.dispatch')
    .factory('horizon.dashboard.dispatch.comService', QDRService);

  QDRService.$inject = [
    '$rootScope',
    '$http',
    '$timeout',
    '$location',
    'horizon.dashboard.dispatch.basePath',
  ];

  function QDRService($rootScope, $http, $timeout, $location, basePath) {
    var self = {

	  rhea: require("rhea"),

      timeout: 10,
      connectActions: [],
      disconnectActions: [],
      updatedActions: {},
      stop: undefined,  // update interval handle

      addConnectAction: function(action) {
        if (angular.isFunction(action)) {
          self.connectActions.push(action);
        }
      },
      addDisconnectAction: function(action) {
        if (angular.isFunction(action)) {
          self.disconnectActions.push(action);
        }
      },
      addUpdatedAction: function(key, action) {
        if (angular.isFunction(action)) {
            self.updatedActions[key] = action;
        }
      },
      delUpdatedAction: function(key) {
        if (key in self.updatedActions)
            delete self.updatedActions[key];
      },

      executeConnectActions: function() {
        self.connectActions.forEach(function(action) {
          //QDR.log.debug("executing connect action " + action);
			try {
                action.apply();
            } catch (e) {
                // in case the page that registered the handler has been unloaded
            }
        });
        self.connectActions = [];

      },
      executeDisconnectActions: function() {
        self.disconnectActions.forEach(function(action) {
			try {
                action.apply();
            } catch (e) {
                // in case the page that registered the handler has been unloaded
            }
        });
        self.disconnectActions = [];
      },
      executeUpdatedActions: function() {
        for (var action in self.updatedActions) {
			try {
                self.updatedActions[action].apply();
            } catch (e) {
                delete self.updatedActions[action]
            }
        }
      },
	redirectWhenConnected: function (org) {
		//$location.path(basePath + "/connect")
		//$location.search('org', org);
           window.location.replace("/connect/");
	},

      notifyTopologyDone: function() {
        //QDR.log.debug("got Toplogy done notice");

        if (!angular.isDefined(self.schema))
            return;
        else if (self.topology._gettingTopo)
            return;
        if (!self.gotTopology) {
            QDR.log.debug("topology was just initialized");
            self.gotTopology = true;
            self.executeConnectActions();
            $rootScope.$apply();
        } else {
            //QDR.log.debug("topology model was just updated");
            self.executeUpdatedActions();
        }

      },
      /**
       * @property options
       * Holds a reference to the connection options when
       * a connection is started
       */
      options: undefined,

      /*
       * @property message
       * The proton message that is used to send commands
       * and receive responses
       */
		sender: undefined,
		receiver: undefined,
		sendable: false,

      schema: undefined,

      toAddress: undefined,
      connected: false,
      gotTopology: false,
      errorText: undefined,
	  connectionError: undefined,

      isConnected: function() {
        return self.connected;
      },

    correlator: {
        _objects: {},
        _corremationID: 0,

        corr: function () {
            var id = ++this._corremationID + "";
			this._objects[id] = {resolver: null}
            return id;
        },
        request: function() {
            //QDR.log.debug("correlator:request");
            return this;
        },
        then: function(id, resolver, error) {
            //QDR.log.debug("registered then resolver for correlationID: " + id);
			if (error) {
	            delete this._objects[id];
				return;
			}
            this._objects[id].resolver = resolver;
        },
        // called by receiver's on('message') handler when a response arrives
        resolve: function(context) {
			var correlationID = context.message.properties.correlation_id;
            this._objects[correlationID].resolver(context.message.body, context);
            delete this._objects[correlationID];
        }
    },

    onSubscription: function() {
        self.getSchema();
     },

    startUpdating: function () {
        self.stopUpdating();
        QDR.log.info("startUpdating called")
        self.topology.get();
        self.stop = setInterval(function() {
            self.topology.get();
        }, 2000);
    },
    stopUpdating: function () {
        if (angular.isDefined(self.stop)) {
            QDR.log.info("stopUpdating called")
            clearInterval(self.stop);
            self.stop = undefined;
        }
    },

      initProton: function() {
        //self.loadConnectOptions()
      },
      cleanUp: function() {
      },
      error: function(line) {
        if (line.num) {
          QDR.log.debug("error - num: ", line.num, " message: ", line.message);
        } else {
          QDR.log.debug("error - message: ", line.message);
        }
      },
      disconnected: function(line) {
        QDR.log.debug("Disconnected from QDR server");
        self.executeDisconnectActions();
      },

      nameFromId: function (id) {
		    return id.split('/')[3];
      },

      humanify: function (s) {
          if (!s || s.length === 0)
			return s;
          var t = s.charAt(0).toUpperCase() + s.substr(1).replace(/[A-Z]/g, ' $&');
          return t.replace(".", " ");
      },
	  pretty: function(v) {
    	var formatComma = d3.format(",");
		if (!isNaN(parseFloat(v)) && isFinite(v))
			return formatComma(v);
		return v;
	  },

      nodeNameList: function() {
        var nl = [];
        // if we are in the middel of updating the topology
        // then use the last known node info
        var ni = self.topology._nodeInfo;
        if (self.topology._gettingTopo)
            ni = self.topology._lastNodeInfo;
		for (var id in ni) {
            nl.push(self.nameFromId(id));
        }
        return nl.sort();
      },

      nodeIdList: function() {
        var nl = [];
        // if we are in the middel of updating the topology
        // then use the last known node info
        var ni = self.topology._nodeInfo;
        if (self.topology._gettingTopo)
            ni = self.topology._lastNodeInfo;
		for (var id in ni) {
            nl.push(id);
        }
        return nl.sort();
      },

      nodeList: function () {
        var nl = [];
        var ni = self.topology._nodeInfo;
        if (self.topology._gettingTopo)
            ni = self.topology._lastNodeInfo;
		for (var id in ni) {
            nl.push({name: self.nameFromId(id), id: id});
        }
        return nl;
      },

      // given an attribute name array, find the value at the same index in the values array
      valFor: function (aAr, vAr, key) {
          var idx = aAr.indexOf(key);
          if ((idx > -1) && (idx < vAr.length)) {
              return vAr[idx];
          }
          return null;
      },

		isArtemis: function (d) {
			return d.nodeType ==='on-demand' && !d.properties.product;
		},

		isQpid: function (d) {
			return d.nodeType ==='on-demand' && (d.properties && d.properties.product === 'qpid-cpp');
		},

		isAConsole: function (properties, connectionId, nodeType, key) {
			return self.isConsole({properties: properties, connectionId: connectionId, nodeType: nodeType, key: key})
		},
		isConsole: function (d) {
			// use connection properties if available
			if (d && d['properties'] && d['properties']['console_identifier'] == 'Dispatch console')
				return true;
			return false;
		},

		flatten: function (attributes, result) {
			var flat = {}
			attributes.forEach( function (attr, i) {
				if (result && result.length > i)
					flat[attr] = result[i]
			})
			return flat;
		},
		isConsoleLink: function (link) {
			// find the connection for this link
			var conns = self.topology.nodeInfo()[link.nodeId]['.connection']
			var connIndex = conns.attributeNames.indexOf("identity")
			var linkCons = conns.results.filter ( function (conn) {
				return conn[connIndex] === link.connectionId;
			})
			var conn = self.flatten(conns.attributeNames, linkCons[0]);

			return self.isConsole(conn)
		},

		quiesceLink: function (nodeId, name) {
			function gotMethodResponse (nodeName, entity, response, context) {
				var statusCode = context.message.application_properties.statusCode;
				if (statusCode < 200 || statusCode >= 300) {
					Core.notification('error', context.message.application_properties.statusDescription);
				}
			}
			var attributes = {adminStatus: 'disabled', name: name};
			self.sendMethod(nodeId, "router.link", attributes, "UPDATE", undefined, gotMethodResponse)
		},

    connectionOptions: {address: '0.0.0.0', port: 5673},
    loadConnectOptions: function (callback) {
      $http.get(basePath + 'connect.json').
        success(function(data, status, headers, config) {
        //QDR.log.debug("got connect info from file")
        //console.dump(data)
          self.connectionOptions = data;
          if (callback)
            callback()
        }).
        error(function(data, status, headers, config) {
        //QDR.log.debug("did not get connect info from file")
        //console.dump(status)
          if (callback)
            callback()
        });
    },

		addr_text: function (addr) {
	        if (!addr)
	            return "-"
	        if (addr[0] == 'M')
	            return addr.substring(2)
	        else
	            return addr.substring(1)
		},
		addr_class: function (addr) {
			if (!addr) return "-"
	        if (addr[0] == 'M')  return "mobile"
	        if (addr[0] == 'R')  return "router"
	        if (addr[0] == 'A')  return "area"
	        if (addr[0] == 'L')  return "local"
	        if (addr[0] == 'C')  return "link-incoming"
	        if (addr[0] == 'D')  return "link-outgoing"
	        if (addr[0] == 'T')  return "topo"
	        return "unknown: " + addr[0]
		},
		identity_clean: function (identity) {
	        if (!identity)
	            return "-"
	        var pos = identity.indexOf('/')
	        if (pos >= 0)
	            return identity.substring(pos + 1)
	        return identity
		},

      /*
       * send the management messages that build up the topology
       *
       *
       */
      topology: {
        _gettingTopo: false,
        _nodeInfo: {},
        _lastNodeInfo: {},
        _expected: {},
        _timerHandle: null,

        nodeInfo: function () {
            return this._gettingTopo ? this._lastNodeInfo : this._nodeInfo;
        },

        get: function () {
            if (this._gettingTopo)
                return;
            if (!self.connected) {
				QDR.log.debug("topology get failed because !self.connected")
                return;
            }
            this._lastNodeInfo = angular.copy(this._nodeInfo);
            this._gettingTopo = true;

            self.errorText = undefined;
            this.cleanUp(this._nodeInfo);
            this._nodeInfo = {};
            this._expected = {};

            // get the list of nodes to query.
            // once this completes, we will get the info for each node returned
            self.getRemoteNodeInfo( function (response, context) {
                //QDR.log.debug("got remote node list of ");
                //console.dump(response);
                if( Object.prototype.toString.call( response ) === '[object Array]' ) {
					if (response.length === 0) {
						// there is only one router, get its node id from the reeciiver
						//"amqp:/_topo/0/Router.A/temp.aSO3+WGaoNUgGVx"
						var address = context.receiver.remote.attach.source.address;
						var addrParts = address.split('/')
						addrParts.splice(addrParts.length-1, 1, '$management')
						response = [addrParts.join('/')]
					}
                    // we expect a response for each of these nodes
                    self.topology.wait(self.timeout);
                    for (var i=0; i<response.length; ++i) {
                        self.makeMgmtCalls(response[i]);
                    }
                };
            });
        },

        cleanUp: function (obj) {
            //if (obj)
            //    delete obj;
        },
        wait: function (timeout) {
            this.timerHandle = setTimeout(this.timedOut, timeout * 1000);
         },
        timedOut: function () {
        // a node dropped out. this happens when the get-mgmt-nodex
        // results contains more nodes than actually respond within
        // the timeout. However, if the responses we get don't contain
        // the missing node, assume we are done.
            QDR.log.info("timed out waiting for management responses");
            // note: can't use 'this' in a timeout handler
            self.topology.miniDump("state at timeout");
            // check if _nodeInfo is consistent
            if (self.topology.isConsistent()) {
                //TODO: notify controllers which node was dropped
                // so they can keep an event log
                self.topology.ondone();
                return;
            }
            self.topology.onerror(Error("Timed out waiting for management responses"));
        },
        isConsistent: function () {
            // see if the responses we have so far reference any nodes
            // for which we don't have a response
            var gotKeys = {};
            for (var id in this._nodeInfo) {
                var onode = this._nodeInfo[id];
                var conn = onode['.connection'];
                // get list of node names in the connection data
                if (conn) {
                    var containerIndex = conn.attributeNames.indexOf('container');
                    var connectionResults = conn.results;
                    if (containerIndex >= 0)
                        for (var j=0; j < connectionResults.length; ++j) {
                            // inter-router connection to a valid dispatch connection name
                            gotKeys[connectionResults[j][containerIndex]] = ""; // just add the key
                        }
                }
            }
            // gotKeys now contains all the container names that we have received
            // Are any of the keys that are still expected in the gotKeys list?
            var keys = Object.keys(gotKeys);
            for (var id in this._expected) {
                var key = self.nameFromId(id);
                if (key in keys)
                    return false;
            }
            return true;
        },

        addNodeInfo: function (id, entity, values) {
            // save the results in the nodeInfo object
            if (id) {
                if (!(id in self.topology._nodeInfo)) {
                    self.topology._nodeInfo[id] = {};
                }
                self.topology._nodeInfo[id][entity] = values;
            }

            // remove the id / entity from _expected
            if (id in self.topology._expected) {
                var entities = self.topology._expected[id];
                var idx = entities.indexOf(entity);
                if (idx > -1) {
                    entities.splice(idx, 1);
                    if (entities.length == 0)
                        delete self.topology._expected[id];
                }
            }
            // see if the expected obj is empty
            if (Object.getOwnPropertyNames(self.topology._expected).length == 0)
                self.topology.ondone();
            self.topology.cleanUp(values);
        },
        expect: function (id, key) {
            if (!key || !id)
                return;
            if (!(id in this._expected))
                this._expected[id] = [];
            if (this._expected[id].indexOf(key) == -1)
                this._expected[id].push(key);
        },
        ondone: function () {
            clearTimeout(this.timerHandle);
            this._gettingTopo = false;
            //this.miniDump();
            //this.dump();
            self.notifyTopologyDone();
         },
         dump: function (prefix) {
            if (prefix)
                QDR.log.info(prefix);
            QDR.log.info("---");
            for (var key in this._nodeInfo) {
                QDR.log.info(key);
                console.dump(this._nodeInfo[key]);
                QDR.log.info("---");
            }
            QDR.log.debug("was still expecting:");
            console.dump(this._expected);
        },
         miniDump: function (prefix) {
            if (prefix)
                QDR.log.info(prefix);
            QDR.log.info("---");
            console.dump(Object.keys(this._nodeInfo));
            QDR.log.info("---");
        },
        onerror: function (err) {
            this._gettingTopo = false;
            QDR.log.debug("Err:" + err);
            self.executeDisconnectActions();

        }

      },

      getRemoteNodeInfo: function (callback) {
	 	//QDR.log.debug("getRemoteNodeInfo called");
        var ret;
        // first get the list of remote node names
	 	self.correlator.request(
                ret = self.sendMgmtQuery('GET-MGMT-NODES')
            ).then(ret.id, function(response, context) {
                callback(response, context);
                self.topology.cleanUp(response);
            }, ret.error);
      },

      makeMgmtCalls: function (id) {
            var keys = [".router", ".connection", ".container", ".router.node", ".listener", ".router.link"];
            $.each(keys, function (i, key) {
                self.topology.expect(id, key);
                self.getNodeInfo(id, key, [], self.topology.addNodeInfo);
            });
      },

      getNodeInfo: function (nodeName, entity, attrs, callback) {
        //QDR.log.debug("getNodeInfo called with nodeName: " + nodeName + " and entity " + entity);
        var ret;
        self.correlator.request(
            ret = self.sendQuery(nodeName, entity, attrs)
        ).then(ret.id, function(response) {
            callback(nodeName, entity, response);
            //self.topology.addNodeInfo(nodeName, entity, response);
            //self.topology.cleanUp(response);
        }, ret.error);
      },

		getMultipleNodeInfo: function (nodeNames, entity, attrs, callback, selectedNodeId, aggregate) {
			if (!angular.isDefined(aggregate))
				aggregate = true;
			var responses = {};
			var gotNodesResult = function (nodeName, dotentity, response) {
				responses[nodeName] = response;
				if (Object.keys(responses).length == nodeNames.length) {
					if (aggregate)
						self.aggregateNodeInfo(nodeNames, entity, selectedNodeId, responses, callback);
					else {
						callback(nodeNames, entity, responses)
					}
				}
			}

			nodeNames.forEach( function (id) {
	            self.getNodeInfo(id, '.'+entity, attrs, gotNodesResult);
	        })
			//TODO: implement a timeout in case not all requests complete
		},

		aggregateNodeInfo: function (nodeNames, entity, selectedNodeId, responses, callback) {
			//QDR.log.debug("got all results for  " + entity);
			// aggregate the responses
			var newResponse = {};
			var thisNode = responses[selectedNodeId];
			newResponse['attributeNames'] = thisNode.attributeNames;
			newResponse['results'] = thisNode.results;
			newResponse['aggregates'] = [];
			for (var i=0; i<thisNode.results.length; ++i) {
				var result = thisNode.results[i];
				var vals = [];
				result.forEach( function (val) {
					vals.push({sum: val, detail: []})
				})
				newResponse.aggregates.push(vals);
			}
			var nameIndex = thisNode.attributeNames.indexOf("name");
			var ent = self.schema.entityTypes[entity];
			var ids = Object.keys(responses);
			ids.sort();
			ids.forEach( function (id) {
				var response = responses[id];
				var results = response.results;
				results.forEach( function (result) {
					// find the matching result in the aggregates
					var found = newResponse.aggregates.some( function (aggregate, j) {
						if (aggregate[nameIndex].sum === result[nameIndex]) {
							// result and aggregate are now the same record, add the graphable values
							newResponse.attributeNames.forEach( function (key, i) {
								if (ent.attributes[key] && ent.attributes[key].graph) {
									if (id != selectedNodeId)
										aggregate[i].sum += result[i];
								}
								aggregate[i].detail.push({node: self.nameFromId(id)+':', val: result[i]})
							})
							return true; // stop looping
						}
						return false; // continute looking for the aggregate record
					})
					if (!found) {
						// this attribute was not found in the aggregates yet
						// because it was not in the selectedNodeId's results
						var vals = [];
						result.forEach( function (val) {
							vals.push({sum: val, detail: [{node: self.nameFromId(id), val: val}]})
						})
						newResponse.aggregates.push(vals)
					}
				})
			})
			callback(nodeNames, entity, newResponse);
		},


      getSchema: function () {
        //QDR.log.debug("getting schema");
        var ret;
        self.correlator.request(
            ret = self.sendMgmtQuery('GET-SCHEMA')
        ).then(ret.id, function(response) {
            //QDR.log.debug("Got schema response");
			// remove deprecated
			for (var entityName in response.entityTypes) {
				var entity = response.entityTypes[entityName]
				if (entity.deprecated) {
					// deprecated entity
				    delete response.entityTypes[entityName]
				} else {
					for (var attributeName in entity.attributes) {
						var attribute = entity.attributes[attributeName]
						if (attribute.deprecated) {
							// deprecated attribute
							delete response.entityTypes[entityName].attributes[attributeName]
						}
					}
				}
			}
			self.schema = response;
	        self.topology.get();
        }, ret.error);
      },

      getNodeInfo: function (nodeName, entity, attrs, callback) {
        //QDR.log.debug("getNodeInfo called with nodeName: " + nodeName + " and entity " + entity);
        var ret;
        self.correlator.request(
            ret = self.sendQuery(nodeName, entity, attrs)
        ).then(ret.id, function(response) {
            callback(nodeName, entity, response);
            //self.topology.addNodeInfo(nodeName, entity, response);
            //self.topology.cleanUp(response);
        }, ret.error);
      },

	sendMethod: function (nodeId, entity, attrs, operation, props, callback) {
		var ret;
		self.correlator.request(
			ret = self._sendMethod(nodeId, entity, attrs, operation, props)
		).then(ret.id, function (response, context) {
				callback(nodeId, entity, response, context);
		}, ret.error);
	},

	_fullAddr: function (toAddr) {
        var toAddrParts = toAddr.split('/');
        if (toAddrParts.shift() != "amqp:") {
            self.topology.error(Error("unexpected format for router address: " + toAddr));
            return;
        }
        //var fullAddr =  self.toAddress + "/" + toAddrParts.join('/');
        var fullAddr =  toAddrParts.join('/');
		return fullAddr;
	},

	_sendMethod: function (toAddr, entity, attrs, operation, props) {
		var fullAddr = self._fullAddr(toAddr);
		var ret = {id: self.correlator.corr()};
		if (!self.sender || !self.sendable) {
			ret.error = "no sender"
			return ret;
		}
		try {
			var application_properties = {
				operation:  operation
			}
			if (entity) {
				var ent = self.schema.entityTypes[entity];
				var fullyQualifiedType = ent ? ent.fullyQualifiedType : entity;
				application_properties.type = fullyQualifiedType || entity;
			}
			if (attrs.name)
				application_properties.name = attrs.name;
			if (props) {
				jQuery.extend(application_properties, props);
			}
			var msg = {
	                body: attrs,
	                properties: {
	                    to:                     fullAddr,
                        reply_to:               self.receiver.remote.attach.source.address,
	                    correlation_id:         ret.id
	                },
	                application_properties: application_properties
            }
            self.sender.send( msg );
			console.dump("------- method called -------")
            console.dump (msg)
		}
		catch (e) {
			error = "error sending: " + e;
			QDR.log.error(error)
			ret.error = error;
		}
		return ret;
	},

    sendQuery: function(toAddr, entity, attrs, operation) {
        operation = operation || "QUERY"
		var fullAddr = self._fullAddr(toAddr);

		var body;
        if (attrs)
            body = {
                    "attributeNames": attrs,
            }
        else
            body = {
                "attributeNames": [],
            }
		if (entity[0] === '.')
			entity = entity.substr(1, entity.length-1)
		var prefix = "org.apache.qpid.dispatch."
		var configs = ["address", "autoLink", "linkRoute"]
		if (configs.indexOf(entity) > -1)
			prefix += "router.config."
		return self._send(body, fullAddr, operation, prefix + entity);
    },

    sendMgmtQuery: function (operation) {
		return self._send([], "/$management", operation);
    },

	_send: function (body, to, operation, entityType) {
		var ret = {id: self.correlator.corr()};
		if (!self.sender || !self.sendable) {
			ret.error = "no sender"
			return ret;
		}
		try {
			var application_properties = {
				operation:  operation,
                type:       "org.amqp.management",
                name:       "self"
            };
			if (entityType)
                application_properties.entityType = entityType;

	        self.sender.send({
	                body: body,
	                properties: {
	                    to:                     to,
                        reply_to:               self.receiver.remote.attach.source.address,
	                    correlation_id:         ret.id
	                },
	                application_properties: application_properties
            })
		}
		catch (e) {
			error = "error sending: " + e;
			QDR.log.error(error)
			ret.error = error;
		}
		return ret;
	},

      disconnect: function() {
        self.connection.close();
		self.errorText = "Disconnected."
      },

      connect: function(overrideConnectOptions) {
  			QDR.log.debug("****** calling rhea.connect ********")
        var options = self.connectionOptions;
        if (overrideConnectOptions)
          options = overrideConnectOptions;
        self.topologyInitialized = false;
		    if (!self.connected) {
			    var okay = {connection: false, sender: false, receiver: false}
          var port = options.port || 5673;
          var baseAddress = options.address + ':' + port;
			    var ws = self.rhea.websocket_connect(WebSocket);
			    self.toAddress = "amqp://" + baseAddress;
			    self.connectionError = undefined;

			var stop = function (context) {
				//self.stopUpdating();
				okay.sender = false;
				okay.receiver = false;
				okay.connected = false;
				self.connected = false;
				self.sender = null;
				self.receiver = null;
				self.sendable = false;
				self.gotTopology = false;
			}
			var maybeStart = function () {
				if (okay.connection && okay.sender && okay.receiver && self.sendable && !self.connected) {
					QDR.log.info("okay to start")
					self.connected = true;
					self.connection = connection;
					self.sender = sender;
					self.receiver = receiver;
					self.onSubscription();
					self.gotTopology = false;
				}
			}
			var onDisconnect = function () {
				//QDR.log.warn("Disconnected");
				self.connectionError = true;
				stop();
				self.executeDisconnectActions();
			}

			var connection;
			try {
QDR.log.debug("trying to connect to ws://" + baseAddress)
                connection = self.rhea.connect({
                    connection_details:ws('ws://' + baseAddress, ["binary", "base64", "AMQWSB10"]),
                    reconnect:true,
                    properties: {console_identifier: 'Dispatch console'}
	            });
			}
			catch (e) {
				QDR.log.debug("exception caught on connect")
				self.errorText = "Connection failed"
				onDisconnect();
			}
			if (!self.connectionError) {
				connection.on('connection_open', function (context) {
					QDR.log.debug("connection_opened")
					okay.connection = true;
					okay.receiver = false;
					okay.sender = false;
				})
				connection.on('disconnected', function (context) {
					QDR.log.debug("connection disconnected")
					self.errorText = "Unable to connect"
					onDisconnect();
				})
				connection.on('connection_close', function (context) {
					QDR.log.debug("connection closed")
					self.errorText = "Disconnected"
					onDisconnect();
				})

				var sender = connection.open_sender();
				sender.on('sender_open', function (context) {
					QDR.log.debug("sender_opened")
					okay.sender = true
					maybeStart()
				})
				sender.on('sendable', function (context) {
					//QDR.log.debug("sendable")
					self.sendable = true;
					maybeStart();
				})

				var receiver = connection.open_receiver({source: {dynamic: true}});
				receiver.on('receiver_open', function (context) {
					QDR.log.debug("receiver_opened")
					okay.receiver = true;
					maybeStart()
				})
				receiver.on("message", function (context) {
					self.correlator.resolve(context);
				});
			}
		}
      }
    }
    return self;
  };

  return QDR;
}(QDR || {}));
