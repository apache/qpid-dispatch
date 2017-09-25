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
/**
 * @module QDR
 */
var QDR = (function(QDR) {

  // The QDR service handles the connection to
  // the server in the background
  QDR.module.factory("QDRService", ['$rootScope', '$http', '$timeout', '$resource', '$location', function($rootScope, $http, $timeout, $resource, $location) {
    var self = {

      rhea: require("rhea"),
      timeout: 10,             // seconds to wait before assuming a request has failed
      updateInterval: 2000,   // milliseconds between background updates
      connectActions: [],
      disconnectActions: [],
      updatedActions: {},
      updating: false,        // are we updating the node list in the background
      maxCorrelatorDepth: 10, // max number of outstanding requests to allow

      /*
       * @property message
       * The proton message that is used to send commands
       * and receive responses
       */
      sender: undefined,
      receiver: undefined,
      version: undefined,
      sendable: false,

      schema: undefined,

      connected: false,
      gotTopology: false,
      errorText: undefined,
      connectionError: undefined,

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
      delDisconnectAction: function(action) {
        if (angular.isFunction(action)) {
          var index = self.disconnectActions.indexOf(action)
          if (index >= 0)
            self.disconnectActions.splice(index, 1)
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
          try {
            action.apply();
          } catch (e) {
            // in case the page that registered the handler has been unloaded
            QDR.log.info(e.message)
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
        for (action in self.updatedActions) {
//          try {
            self.updatedActions[action].apply();
/*          } catch (e) {
QDR.log.debug("caught error executing updated actions")
console.dump(e)
            delete self.updatedActions[action]
          }
          */
        }
      },
      redirectWhenConnected: function(org) {
        $location.path(QDR.pluginRoot + "/connect")
        $location.search('org', org);
      },

      notifyTopologyDone: function() {
        if (!self.gotTopology) {
          QDR.log.debug("topology was just initialized");
          //console.dump(self.topology._nodeInfo)
          self.gotTopology = true;
          //$rootScope.$apply();
        } else {
          //QDR.log.debug("topology model was just updated");
        }
        self.executeUpdatedActions();

      },

      isConnected: function() {
        return self.connected;
      },

      versionCheck: function (minVer) {
        var verparts = self.version.split('.')
        var minparts = minVer.split('.')
        try {
          for (var i=0; i<minparts.length; ++i) {
            if (parseInt(minVer[i] > parseInt(verparts[i])))
              return false
          }
        } catch (e) {
          QDR.log.debug("error doing version check between: " + self.version + " and " + minVer + " " + e.message)
          return false
        }
        return true
      },

      correlator: {
        _objects: {},
        _correlationID: 0,

        corr: function() {
          var id = ++this._correlationID + "";
          this._objects[id] = {
            resolver: null
          }
          return id;
        },
        request: function() {
          //QDR.log.debug("correlator:request");
          return this;
        },
        then: function(id, resolver, error) {
          //QDR.log.debug("registered then resolver for correlationID: " + id);
          if (error) {
            //QDR.log.debug("then received an error. deleting correlator")
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
        },
        depth: function () {
          return Object.keys(this._objects).length
        }
      },

      onSubscription: function() {
        self.executeConnectActions();
        var org = $location.search()
        if (org)
          org = org.org
        if (org && org.length > 0 && org !== "connect") {
          self.getSchema(function () {
            self.setUpdateEntities([])
            self.topology.get()
            self.addUpdatedAction('onSub', function () {
              self.delUpdatedAction('onSub')
              $timeout( function () {
                $location.path(QDR.pluginRoot + '/' + org)
                $location.search('org', null)
                $location.replace()
              })
            })
          });
        }
      },

      startUpdating: function() {
        self.stopUpdating(true);
        QDR.log.info("startUpdating called")
        self.updating = true;
        self.topology.get();
      },
      stopUpdating: function(silent) {
        self.updating = false;
        if (self.topology._getTimer) {
          clearTimeout(self.topology._getTimer)
          self.topology._getTimer = null;
        }
        if (self.topology._waitTimer) {
          clearTimeout(self.topology._waitTimer)
          self.topology._waitTimer = null;
        }
        if (self.topology._gettingTopo) {
          if (self.topology.q)
            self.topology.q.abort()
        }
        if (!silent)
          QDR.log.info("stopUpdating called")
      },

      cleanUp: function() {},
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

      nameFromId: function(id) {
        return id.split('/')[3];
      },

      humanify: function(s) {
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
        for (var id in self.topology._nodeInfo) {
          nl.push(self.nameFromId(id));
        }
        return nl.sort();
      },

      nodeIdList: function() {
        var nl = [];
        for (var id in self.topology._nodeInfo) {
          nl.push(id);
        }
        return nl.sort();
      },

      nodeList: function() {
        var nl = [];
        for (var id in self.topology._nodeInfo) {
          nl.push({
            name: self.nameFromId(id),
            id: id
          });
        }
        return nl;
      },

      isLargeNetwork: function () {
        return Object.keys(self.topology._nodeInfo).length >= 12
      },
      isMSIE: function () {
        return (document.documentMode || /Edge/.test(navigator.userAgent))
      },

      // given an attribute name array, find the value at the same index in the values array
      valFor: function(aAr, vAr, key) {
        var idx = aAr.indexOf(key);
        if ((idx > -1) && (idx < vAr.length)) {
          return vAr[idx];
        }
        return null;
      },

      isArtemis: function(d) {
        return (d.nodeType === 'route-container' || d.nodeType === 'on-demand') && (d.properties && d.properties.product === 'apache-activemq-artemis');
      },

      isQpid: function(d) {
        return (d.nodeType === 'route-container' || d.nodeType === 'on-demand') && (d.properties && d.properties.product === 'qpid-cpp');
      },

      isAConsole: function(properties, connectionId, nodeType, key) {
        return self.isConsole({
          properties: properties,
          connectionId: connectionId,
          nodeType: nodeType,
          key: key
        })
      },
      isConsole: function(d) {
        // use connection properties if available
        return (d && d['properties'] && d['properties']['console_identifier'] === 'Dispatch console')
      },

      flatten: function(attributes, result) {
        var flat = {}
        attributes.forEach(function(attr, i) {
          if (result && result.length > i)
            flat[attr] = result[i]
        })
        return flat;
      },
      isConsoleLink: function(link) {
        // find the connection for this link
        var conns = self.topology.nodeInfo()[link.nodeId]['.connection']
        var connIndex = conns.attributeNames.indexOf("identity")
        var linkCons = conns.results.filter(function(conn) {
          return conn[connIndex] === link.connectionId;
        })
        var conn = self.flatten(conns.attributeNames, linkCons[0]);

        return self.isConsole(conn)
      },

      quiesceLink: function(nodeId, name) {
        function gotMethodResponse(nodeName, entity, response, context) {
          var statusCode = context.message.application_properties.statusCode;
          if (statusCode < 200 || statusCode >= 300) {
            Core.notification('error', context.message.statusDescription);
            QDR.log.info('Error ' + context.message.statusDescription)
          }
        }
        var attributes = {
          adminStatus: 'disabled',
          name: name
        };
        self.sendMethod(nodeId, "router.link", attributes, "UPDATE", undefined, gotMethodResponse)
      },
      addr_text: function(addr) {
        if (!addr)
          return "-"
        if (addr[0] == 'M')
          return addr.substring(2)
        else
          return addr.substring(1)
      },
      addr_class: function(addr) {
        if (!addr) return "-"
        if (addr[0] == 'M') return "mobile"
        if (addr[0] == 'R') return "router"
        if (addr[0] == 'A') return "area"
        if (addr[0] == 'L') return "local"
        if (addr[0] == 'C') return "link-incoming"
        if (addr[0] == 'E') return "link-incoming"
        if (addr[0] == 'D') return "link-outgoing"
        if (addr[0] == 'F') return "link-outgoing"
        if (addr[0] == 'T') return "topo"
        return "unknown: " + addr[0]
      },
      identity_clean: function(identity) {
        if (!identity)
          return "-"
        var pos = identity.indexOf('/')
        if (pos >= 0)
          return identity.substring(pos + 1)
        return identity
      },

      queueDepth: function () {
        var qdepth = self.maxCorrelatorDepth - self.correlator.depth()
        if (qdepth <= 0)
          qdepth = 1;
//QDR.log.debug("queueDepth requested " + qdepth + "(" + self.correlator.depth() + ")")
        return qdepth;
      },
      // check if all nodes have this entity. if not, get them
      initEntity: function (entity, callback) {
        var callNeeded = Object.keys(self.topology._nodeInfo).some( function (node) {
          return !angular.isDefined(self.topology._nodeInfo[node][entity])
        })
        if (callNeeded) {
          self.loadEntity(entity, callback)
        } else
          callback()
      },

      // get/refresh entities for all nodes
      loadEntity: function (entities, callback) {
        if (Object.prototype.toString.call(entities) !== '[object Array]') {
          entities = [entities]
        }
        var q = QDR.queue(self.queueDepth())
        for (node in self.topology._nodeInfo) {
          for (var i=0; i<entities.length; ++i) {
            var entity = entities[i]
            q.defer(self.ensureNodeInfo, node, entity, [], q)
          }
        }
        q.await(function (error) {
          clearTimeout(self.topology._waitTimer)
          callback();
        })
      },

      // enusre all the topology nones have all these entities
      ensureAllEntities: function (entityAttribs, callback, extra) {
        self.ensureEntities(Object.keys(self.topology._nodeInfo), entityAttribs, callback, extra)
      },

      // ensure these nodes have all these entities. don't fetch unless forced to
      ensureEntities: function (nodes, entityAttribs, callback, extra) {
        if (Object.prototype.toString.call(entityAttribs) !== '[object Array]') {
          entityAttribs = [entityAttribs]
        }
        if (Object.prototype.toString.call(nodes) !== '[object Array]') {
          nodes = [nodes]
        }
        var q = QDR.queue(self.queueDepth())
        for (var n=0; n<nodes.length; ++n) {
          for (var i=0; i<entityAttribs.length; ++i) {
            var ea = entityAttribs[i]
            // if we don'e already have the entity or we want to force a refresh
            if (!self.topology._nodeInfo[nodes[n]][ea.entity] || ea.force)
              q.defer(self.ensureNodeInfo, nodes[n], ea.entity, ea.attrs || [], q)
          }
        }
        q.await(function (error) {
          clearTimeout(self.topology._waitTimer)
          callback(extra);
        })
      },

      // queue up a request to get certain attributes for one entity for a node and return the results
      fetchEntity: function (node, entity, attrs, callback) {
        var results = {}
        var gotResponse = function (nodeName, dotentity, response) {
          results = response
        }
        var q = QDR.queue(self.queueDepth())
        q.defer(self.fetchNodeInfo, node, entity, attrs, q, gotResponse)
        q.await(function (error) {
          callback(node, entity, results)
        })
      },

      // get/refreshes entities for all topology.nodes
      // call doneCallback when all data is available
      // optionally supply a resultCallBack that will be called as each result is avaialble
      // if a resultCallBack is supplied, the calling function is responsible for accumulating the responses
      //   otherwise the responses will be returned to the doneCallback as an object
      fetchAllEntities: function (entityAttribs, doneCallback, resultCallback) {
        var q = QDR.queue(self.queueDepth())
        var results = {}
        if (!resultCallback) {
          resultCallback = function (nodeName, dotentity, response) {
            if (!results[nodeName])
              results[nodeName] = {}
            results[nodeName][dotentity] = angular.copy(response);
          }
        }
        var gotAResponse = function (nodeName, dotentity, response) {
          resultCallback(nodeName, dotentity, response)
        }
        if (Object.prototype.toString.call(entityAttribs) !== '[object Array]') {
          entityAttribs = [entityAttribs]
        }
        var nodes = Object.keys(self.topology._nodeInfo)
        for (var n=0; n<nodes.length; ++n) {
          for (var i=0; i<entityAttribs.length; ++i) {
            var ea = entityAttribs[i]
            q.defer(self.fetchNodeInfo, nodes[n], ea.entity, ea.attrs || [], q, gotAResponse)
          }
        }
        q.await(function (error) {
          doneCallback(results);
        })
      },

      setUpdateEntities: function (entities) {
        self.topology._autoUpdatedEntities = entities
      },
      addUpdateEntity: function (entity) {
        if (self.topology._autoUpdatedEntities.indexOf(entity) == -1)
          self.topology._autoUpdatedEntities.push(entity)
      },
      delUpdateEntity: function (entity) {
        var index = self.topology._autoUpdatedEntities.indexOf(entity)
        if (index != -1)
          self.topology._autoUpdatedEntities.splice(index, 1)
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
        _waitTimer: null,
        _getTimer: null,
        _autoUpdatedEntities: [],
        q: null,

        nodeInfo: function() {
          return self.topology._nodeInfo
        },

        get: function() {
          if (self.topology._gettingTopo) {
            QDR.log.debug("asked to get topology but was already getting it")
            if (self.topology.q)
              self.topology.q.abort()
          }
          self.topology.q = null
          if (!self.connected) {
            QDR.log.debug("topology get failed because !self.connected")
            return;
          }
          if (self.topology._getTimer) {
            clearTimeout(self.topology._getTimer)
            self.topology._getTimer = null
          }

          //QDR.log.info("starting get topology with correlator.depth of " + self.correlator.depth())
          self.topology._gettingTopo = true;
          self.errorText = undefined;

          // get the list of nodes to query.
          // once this completes, we will get the info for each node returned
          self.getRemoteNodeInfo(function(response, context) {
            if (Object.prototype.toString.call(response) === '[object Array]') {
              // remove dropped nodes
              var keys = Object.keys(self.topology._nodeInfo)
              for (var i=0; i<keys.length; ++i) {
                if (response.indexOf(keys[i]) < 0) {
                  delete self.topology._nodeInfo[keys[i]]
                }
              }
              // add any new nodes
              // if there is only one node, it will not be returned
              if (response.length === 0) {
                var parts = self.receiver.remote.attach.source.address.split('/')
                parts[4] = '$management'
                response.push(parts.join('/'))
                //QDR.log.info("GET-MGMT-NODES returned an empty list. Using ")
                //console.dump(response)
              }
              for (var i=0; i<response.length; ++i) {
                if (!angular.isDefined(self.topology._nodeInfo[response[i]])) {
                  self.topology._nodeInfo[angular.copy(response[i])] = {};
                }
              }
              // also refresh any entities that were requested
              self.topology.q = QDR.queue(self.queueDepth())
              for (var i=0; i<self.topology._autoUpdatedEntities.length; ++i) {
                var entity = self.topology._autoUpdatedEntities[i]
                //QDR.log.debug("queuing requests for all nodes for " + entity)
                for (node in self.topology._nodeInfo) {
                  self.topology.q.defer(self.ensureNodeInfo, node, entity, [], self.topology.q)
                }
              }
              self.topology.q.await(function (error) {
                self.topology._gettingTopo = false;
                self.topology.q = null
                self.topology.ondone(error)
              })
            };
          });
        },

        cleanUp: function(obj) {
          if (obj)
            delete obj;
        },
        timedOut: function(q) {
          // a node dropped out. this happens when the get-mgmt-nodex
          // results contains more nodes than actually respond within
          // the timeout
          QDR.log.debug("timed out waiting for management responses");
          // note: can't use 'this' in a timeout handler
          self.topology.miniDump("state at timeout");
          q.abort()
          //self.topology.onerror(Error("management responses are not consistent"));
        },

        addNodeInfo: function(id, entity, values, q) {
          clearTimeout(self.topology._waitTimer)
          // save the results in the nodeInfo object
          if (id) {
            if (!(id in self.topology._nodeInfo)) {
              self.topology._nodeInfo[id] = {};
            }
            // copy the values to allow garbage collector to reclaim their memory
            self.topology._nodeInfo[id][entity] = angular.copy(values)
          }
          self.topology.cleanUp(values);
        },
        ondone: function(waserror) {
          clearTimeout(self.topology._getTimer);
          clearTimeout(self.topology._waitTimer);
          self.topology._waitTimer = null;
          if (self.updating)
            self.topology._getTimer = setTimeout(self.topology.get, self.updateInterval);
          //if (!waserror)
            self.notifyTopologyDone();
        },
        dump: function(prefix) {
          if (prefix)
            QDR.log.info(prefix);
          QDR.log.info("---");
          for (var key in self.topology._nodeInfo) {
            QDR.log.info(key);
            console.dump(self.topology._nodeInfo[key]);
            QDR.log.info("---");
          }
        },
        miniDump: function(prefix) {
          if (prefix)
            QDR.log.info(prefix);
          QDR.log.info("---");
          console.dump(Object.keys(self.topology._nodeInfo));
          QDR.log.info("---");
        },
        onerror: function(err) {
          self.topology._gettingTopo = false;
          QDR.log.debug("Err:" + err);
          //self.executeDisconnectActions();
        }

      },
      getRemoteNodeInfo: function(callback) {
        //QDR.log.debug("getRemoteNodeInfo called");

        setTimeout(function () {
          var ret;
          // first get the list of remote node names
          self.correlator.request(
            ret = self.sendMgmtQuery('GET-MGMT-NODES')
          ).then(ret.id, function(response, context) {
            callback(response, context);
            self.topology.cleanUp(response);
          }, ret.error);
        }, 1)
      },

      // sends a request and updates the topology.nodeInfo object with the response
      // should only be called from a q.defer() statement
      ensureNodeInfo: function (nodeId, entity, attrs, q, callback) {
        //QDR.log.debug("queuing request for " + nodeId + " " + entity)
        self.getNodeInfo(nodeId, entity, attrs, q, function (nodeName, dotentity, response) {
          //QDR.log.debug("got response for " + nodeId + " " + entity)
          self.topology.addNodeInfo(nodeName, dotentity, response, q)
          callback(null)
        })
        return {
          abort: function() {
            delete self.topology._nodeInfo[nodeId]
            //self.topology._nodeInfo[nodeId][entity] = {attributeNames: [], results: [[]]};
          }
        }
      },

      // sends request and returns the response
      // should only be called from a q.defer() statement
      fetchNodeInfo: function (nodeId, entity, attrs, q, heartbeat, callback) {
        self.getNodeInfo(nodeId, entity, attrs, q, function (nodeName, dotentity, response) {
          heartbeat(nodeName, dotentity, response)
          callback(null)
        })
      },

      getMultipleNodeInfo: function(nodeNames, entity, attrs, callback, selectedNodeId, aggregate) {
        if (!angular.isDefined(aggregate))
          aggregate = true;
        var responses = {};
        var gotNodesResult = function(nodeName, dotentity, response) {
          responses[nodeName] = response;
        }

        var q = QDR.queue(self.queueDepth())
        nodeNames.forEach(function(id) {
            q.defer(self.fetchNodeInfo, id, '.' + entity, attrs, q, gotNodesResult)
        })
        q.await(function (error) {
          if (aggregate)
            self.aggregateNodeInfo(nodeNames, entity, selectedNodeId, responses, callback);
          else {
            callback(nodeNames, entity, responses)
          }
        })
      },

      aggregateNodeInfo: function(nodeNames, entity, selectedNodeId, responses, callback) {
        //QDR.log.debug("got all results for  " + entity);
        // aggregate the responses
        var newResponse = {};
        var thisNode = responses[selectedNodeId];
        newResponse['attributeNames'] = thisNode.attributeNames;
        newResponse['results'] = thisNode.results;
        newResponse['aggregates'] = [];
        for (var i = 0; i < thisNode.results.length; ++i) {
          var result = thisNode.results[i];
          var vals = [];
          result.forEach(function(val) {
            vals.push({
              sum: val,
              detail: []
            })
          })
          newResponse.aggregates.push(vals);
        }
        var nameIndex = thisNode.attributeNames.indexOf("name");
        var ent = self.schema.entityTypes[entity];
        var ids = Object.keys(responses);
        ids.sort();
        ids.forEach(function(id) {
          var response = responses[id];
          var results = response.results;
          results.forEach(function(result) {
            // find the matching result in the aggregates
            var found = newResponse.aggregates.some(function(aggregate, j) {
              if (aggregate[nameIndex].sum === result[nameIndex]) {
                // result and aggregate are now the same record, add the graphable values
                newResponse.attributeNames.forEach(function(key, i) {
                  if (ent.attributes[key] && ent.attributes[key].graph) {
                    if (id != selectedNodeId)
                      aggregate[i].sum += result[i];
                  }
                  aggregate[i].detail.push({
                    node: self.nameFromId(id) + ':',
                    val: result[i]
                  })
                })
                return true; // stop looping
              }
              return false; // continute looking for the aggregate record
            })
            if (!found) {
              // this attribute was not found in the aggregates yet
              // because it was not in the selectedNodeId's results
              var vals = [];
              result.forEach(function(val) {
                vals.push({
                  sum: val,
                  detail: [{
                    node: self.nameFromId(id),
                    val: val
                  }]
                })
              })
              newResponse.aggregates.push(vals)
            }
          })
        })
        callback(nodeNames, entity, newResponse);
      },


      getSchema: function(callback) {
        //QDR.log.info("getting schema");
        var ret;
        self.correlator.request(
          ret = self.sendMgmtQuery('GET-SCHEMA')
        ).then(ret.id, function(response) {
          //QDR.log.info("Got schema response");
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
        }, ret.error);
        callback()
      },

      getNodeInfo: function(nodeName, entity, attrs, q, callback) {
        //QDR.log.debug("getNodeInfo called with nodeName: " + nodeName + " and entity " + entity);
        var timedOut = function (q) {
          q.abort()
        }
        var atimer = setTimeout(timedOut, self.timeout * 1000, q);
        var ret;
        self.correlator.request(
          ret = self.sendQuery(nodeName, entity, attrs)
        ).then(ret.id, function(response) {
          clearTimeout(atimer)
          callback(nodeName, entity, response);
        }, ret.error);
      },

      sendMethod: function(nodeId, entity, attrs, operation, props, callback) {
        setTimeout(function () {
          var ret;
          self.correlator.request(
            ret = self._sendMethod(nodeId, entity, attrs, operation, props)
          ).then(ret.id, function(response, context) {
            callback(nodeId, entity, response, context);
          }, ret.error);
        }, 1)
      },

      _fullAddr: function(toAddr) {
        var toAddrParts = toAddr.split('/');
        if (toAddrParts.shift() != "amqp:") {
          self.topology.error(Error("unexpected format for router address: " + toAddr));
          return;
        }
        var fullAddr = toAddrParts.join('/');
        return fullAddr;
      },

      _sendMethod: function(toAddr, entity, attrs, operation, props) {
        var ret = {
          id: self.correlator.corr()
        };
        var fullAddr = self._fullAddr(toAddr);
        if (!self.sender || !self.sendable) {
          ret.error = "no sender"
          return ret;
        }
        try {
          var application_properties = {
            operation: operation
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
              to: fullAddr,
              reply_to: self.receiver.remote.attach.source.address,
              correlation_id: ret.id
            },
            application_properties: application_properties
          }
          self.sender.send(msg);
          //console.dump("------- method called -------")
          //console.dump(msg)
        } catch (e) {
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
        if (attrs) {
          body = {
            "attributeNames": attrs,
          }
        } else {
          body = {
            "attributeNames": [],
          }
        }
        if (entity[0] === '.')
          entity = entity.substr(1, entity.length - 1)
        var prefix = "org.apache.qpid.dispatch."
        var configs = ["address", "autoLink", "linkRoute"]
        if (configs.indexOf(entity) > -1)
          prefix += "router.config."
        return self._send(body, fullAddr, operation, prefix + entity);
      },

      sendMgmtQuery: function(operation) {
        return self._send([], "/$management", operation);
      },

      _send: function(body, to, operation, entityType) {
        var ret = {
          id: self.correlator.corr()
        };
        if (!self.sender || !self.sendable) {
          ret.error = "no sender"
          return ret;
        }
        try {
          var application_properties = {
            operation: operation,
            type: "org.amqp.management",
            name: "self"
          };
          if (entityType)
            application_properties.entityType = entityType;

          self.sender.send({
            body: body,
            properties: {
              to: to,
              reply_to: self.receiver.remote.attach.source.address,
              correlation_id: ret.id
            },
            application_properties: application_properties
          })
        } catch (e) {
          error = "error sending: " + e;
          QDR.log.error(error)
          ret.error = error;
        }
        return ret;
      },

      disconnect: function() {
        self.connection.close();
        self.connected = false
        self.errorText = "Disconnected."
      },

      connectionTimer: null,

      testConnect: function (options, timeout, callback) {
        var connection;
        var allowDelete = true;
        var reconnect = angular.isDefined(options.reconnect) ? options.reconnect : false
        var baseAddress = options.address + ':' + options.port;
        var protocol = "ws"
        if ($location.protocol() === "https")
          protocol = "wss"
        QDR.log.info("testConnect called with reconnect " + reconnect + " using " + protocol + " protocol")
        try {
          var ws = self.rhea.websocket_connect(WebSocket);
          connection = self.rhea.connect({
            connection_details: ws(protocol + "://" + baseAddress, ["binary"]),
            reconnect: reconnect,
              properties: {
                console_identifier: 'Dispatch console'
              }
            }
          );
        } catch (e) {
          QDR.log.debug("exception caught on test connect " + e)
          self.errorText = "Connection failed "
          callback({error: e})
          return
        }
        // called when initial connecting fails, and when connection is dropped after connecting
        connection.on('disconnected', function(context) {
          if (allowDelete) {
            delete connection
            connection.options.reconnect = false
            //QDR.log.info("connection.on(disconnected) called")
            callback({error: "failed to connect"})
          }
        })
        connection.on("connection_open", function (context) {
          allowDelete = false;
          callback({connection: connection, context: context})
        })
      },

      connect: function(options) {
        var connection;
        self.topologyInitialized = false;
        if (!self.connected) {
          var okay = {
            connection: false,
            sender: false,
            receiver: false
          }
          var sender, receiver
          self.connectionError = undefined;

          var stop = function(context) {
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
          var maybeStart = function() {
            if (okay.connection && okay.sender && okay.receiver && self.sendable && !self.connected) {
              //QDR.log.info("okay to start")
              self.connected = true;
              self.connection = connection;
              self.sender = sender;
              self.receiver = receiver;
              self.gotTopology = false;
              self.onSubscription();
            }
          }
          var onDisconnect = function() {
            //QDR.log.warn("Disconnected");
            self.connectionError = true;
            stop();
            self.executeDisconnectActions();
          }

          // called after connection.open event is fired or connection error has happened
          var connectionCallback = function (options) {
            //QDR.log.info('connectionCallback called')
            if (!options.error) {
              //QDR.log.info('there was no error')
              connection = options.connection
              self.version = options.context.connection.properties.version
              QDR.log.debug("connection_opened")
              okay.connection = true;
              okay.receiver = false;
              okay.sender = false;

              connection.on('disconnected', function(context) {
                //QDR.log.info("connection.on(disconnected) called")
                self.errorText = "Unable to connect"
                onDisconnect();
              })
              connection.on('connection_close', function(context) {
                //QDR.log.info("connection closed")
                self.errorText = "Disconnected"
                onDisconnect();
              })

              sender = connection.open_sender();
              sender.on('sender_open', function(context) {
                //QDR.log.info("sender_opened")
                okay.sender = true
                maybeStart()
              })
              sender.on('sendable', function(context) {
                //QDR.log.debug("sendable")
                self.sendable = true;
                maybeStart();
              })

              receiver = connection.open_receiver({
                source: {
                  dynamic: true
                }
              });
              receiver.on('receiver_open', function(context) {
                //QDR.log.info("receiver_opened")
                if (receiver.remote && receiver.remote.attach && receiver.remote.attach.source) {
                  okay.receiver = true;
                  maybeStart()
                }
              })
              receiver.on("message", function(context) {
                self.correlator.resolve(context);
              });
            } else {
              //QDR.log.info("there was an error " + options.error)
              self.errorText = "Unable to connect"
              onDisconnect();
            }
          }

          QDR.log.debug("****** calling rhea.connect ********")
          if (!options.connection) {
            QDR.log.debug("rhea.connect was not passed an existing connection")
            options.reconnect = true
            self.testConnect(options, 5000, connectionCallback)
          } else {
            QDR.log.debug("rhea.connect WAS passed an existing connection")
            connectionCallback(options)
          }
        }
      }
    }
    return self;
  }]);

  return QDR;

}(QDR || {}));

(function() {
  console.dump = function(o) {
    if (window.JSON && window.JSON.stringify)
      QDR.log.info(JSON.stringify(o, undefined, 2));
    else
      console.log(o);
  };
})();

function ngGridFlexibleHeightPlugin (opts) {
    var self = this;
    self.grid = null;
    self.scope = null;
    self.init = function (scope, grid, services) {
        self.domUtilityService = services.DomUtilityService;
        self.grid = grid;
        self.scope = scope;
        var recalcHeightForData = function () { setTimeout(innerRecalcForData, 1); };
        var innerRecalcForData = function () {
            var gridId = self.grid.gridId;
            var footerPanelSel = '.' + gridId + ' .ngFooterPanel';
            if (!self.grid.$topPanel || !self.grid.$canvas)
              return;
            var extraHeight = self.grid.$topPanel.height() + $(footerPanelSel).height();
            var naturalHeight = self.grid.$canvas.height() + 1;
            if (opts != null) {
                if (opts.minHeight != null && (naturalHeight + extraHeight) < opts.minHeight) {
                    naturalHeight = opts.minHeight - extraHeight - 2;
                }
                if (opts.maxHeight != null && (naturalHeight + extraHeight) > opts.maxHeight) {
                    naturalHeight = opts.maxHeight;
                }
            }

            var newViewportHeight = naturalHeight + 3;
            if (!self.scope.baseViewportHeight || self.scope.baseViewportHeight !== newViewportHeight) {
                self.grid.$viewport.css('height', newViewportHeight + 'px');
                self.grid.$root.css('height', (newViewportHeight + extraHeight) + 'px');
                self.scope.baseViewportHeight = newViewportHeight;
                self.domUtilityService.RebuildGrid(self.scope, self.grid);
            }
        };
        self.scope.catHashKeys = function () {
            var hash = '',
                idx;
            for (idx in self.scope.renderedRows) {
                hash += self.scope.renderedRows[idx].$$hashKey;
            }
            return hash;
        };
        self.scope.$watch('catHashKeys()', innerRecalcForData);
        self.scope.$watch(self.grid.config.data, recalcHeightForData);
    };
}

if (!String.prototype.startsWith) {
  String.prototype.startsWith = function (searchString, position) {
    return this.substr(position || 0, searchString.length) === searchString
  }
}

if (!String.prototype.endsWith) {
  String.prototype.endsWith = function(searchString, position) {
      var subjectString = this.toString();
      if (typeof position !== 'number' || !isFinite(position) || Math.floor(position) !== position || position > subjectString.length) {
        position = subjectString.length;
      }
      position -= searchString.length;
      var lastIndex = subjectString.lastIndexOf(searchString, position);
      return lastIndex !== -1 && lastIndex === position;
  };
}

// https://tc39.github.io/ecma262/#sec-array.prototype.findIndex
if (!Array.prototype.findIndex) {
  Object.defineProperty(Array.prototype, 'findIndex', {
    value: function(predicate) {
     // 1. Let O be ? ToObject(this value).
      if (this == null) {
        throw new TypeError('"this" is null or not defined');
      }

      var o = Object(this);

      // 2. Let len be ? ToLength(? Get(O, "length")).
      var len = o.length >>> 0;

      // 3. If IsCallable(predicate) is false, throw a TypeError exception.
      if (typeof predicate !== 'function') {
        throw new TypeError('predicate must be a function');
      }

      // 4. If thisArg was supplied, let T be thisArg; else let T be undefined.
      var thisArg = arguments[1];

      // 5. Let k be 0.
      var k = 0;

      // 6. Repeat, while k < len
      while (k < len) {
        // a. Let Pk be ! ToString(k).
        // b. Let kValue be ? Get(O, Pk).
        // c. Let testResult be ToBoolean(? Call(predicate, T, « kValue, k, O »)).
        // d. If testResult is true, return k.
        var kValue = o[k];
        if (predicate.call(thisArg, kValue, k, o)) {
          return k;
        }
        // e. Increase k by 1.
        k++;
      }

      // 7. Return -1.
      return -1;
    }
  });
}
