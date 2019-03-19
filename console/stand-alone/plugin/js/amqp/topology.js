/*
 * Copyright 2015 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* global Promise d3 Set */

import { utils } from "./utilities.js";

class Topology {
  constructor(connectionManager, interval) {
    this.connection = connectionManager;
    this.updatedActions = {};
    this.entities = []; // which entities to request each topology update
    this.entityAttribs = { connection: [] };
    this._nodeInfo = {}; // info about all known nodes and entities
    this.filtering = false; // filter out nodes that don't have connection info
    this.timeout = 5000;
    this.updateInterval = interval;
    this._getTimer = null;
    this.updating = false;
  }
  addUpdatedAction(key, action) {
    if (typeof action === "function") {
      this.updatedActions[key] = action;
    }
  }
  delUpdatedAction(key) {
    if (key in this.updatedActions) delete this.updatedActions[key];
  }
  executeUpdatedActions(error) {
    for (var action in this.updatedActions) {
      this.updatedActions[action].apply(this, [error]);
    }
  }
  setUpdateEntities(entities) {
    this.entities = entities;
    for (var i = 0; i < entities.length; i++) {
      this.entityAttribs[entities[i]] = [];
    }
  }
  addUpdateEntities(entityAttribs) {
    if (Object.prototype.toString.call(entityAttribs) !== "[object Array]") {
      entityAttribs = [entityAttribs];
    }
    for (var i = 0; i < entityAttribs.length; i++) {
      var entity = entityAttribs[i].entity;
      this.entityAttribs[entity] = entityAttribs[i].attrs || [];
    }
  }
  on(eventName, fn, key) {
    if (eventName === "updated") this.addUpdatedAction(key, fn);
  }
  unregister(eventName, key) {
    if (eventName === "updated") this.delUpdatedAction(key);
  }
  nodeInfo() {
    return this._nodeInfo;
  }
  saveResults(workInfo) {
    let workSet = new Set(Object.keys(workInfo));
    for (let rId in this._nodeInfo) {
      if (!workSet.has(rId)) {
        // mark any routers that went away since the last request as removed
        this._nodeInfo[rId]["removed"] = true;
      } else {
        if (this._nodeInfo[rId]["removed"])
          delete this._nodeInfo[rId]["removed"];
        // copy entities
        for (let entity in workInfo[rId]) {
          if (
            !this._nodeInfo[rId][entity] ||
            workInfo[rId][entity]["timestamp"] + "" >
            this._nodeInfo[rId][entity]["timestamp"] + ""
          ) {
            this._nodeInfo[rId][entity] = utils.copy(workInfo[rId][entity]);
          }
        }
      }
    }
    // add any new routers
    let nodeSet = new Set(Object.keys(this._nodeInfo));
    for (let rId in workInfo) {
      if (!nodeSet.has(rId)) {
        this._nodeInfo[rId] = utils.copy(workInfo[rId]);
      }
    }
  }
  // remove any nodes that don't have connection info
  purge() {
    for (let id in this._nodeInfo) {
      let node = this._nodeInfo[id];
      if (node.removed) {
        delete this._nodeInfo[id];
      }
    }
  }
  get() {
    return new Promise(
      function (resolve, reject) {
        this.connection.sendMgmtQuery("GET-MGMT-NODES").then(
          function (results) {
            let routerIds = results.response;
            if (
              Object.prototype.toString.call(routerIds) === "[object Array]"
            ) {
              // if there is only one node, it will not be returned
              if (routerIds.length === 0) {
                var parts = this.connection.getReceiverAddress().split("/");
                parts[parts.length - 1] = "$management";
                routerIds.push(parts.join("/"));
              }
              let finish = function (workInfo) {
                this.saveResults(workInfo);
                this.onDone(this._nodeInfo);
                resolve(this._nodeInfo);
              };
              let connectedToEdge = function (response, workInfo) {
                let routerId = null;
                if (response.length === 1) {
                  let parts = response[0].split("/");
                  // we are connected to an edge router
                  if (parts[1] === "_edge") {
                    // find the role:edge connection
                    let conn = workInfo[response[0]].connection;
                    if (conn) {
                      let roleIndex = conn.attributeNames.indexOf("role");
                      for (let i = 0; i < conn.results.length; i++) {
                        if (conn.results[i][roleIndex] === "edge") {
                          let container = utils.valFor(
                            conn.attributeNames,
                            conn.results[i],
                            "container"
                          );
                          return utils.idFromName(container, "_topo");
                        }
                      }
                    }
                  }
                }
                return routerId;
              };
              this.doget(routerIds).then(
                function (workInfo) {
                  // test for edge case
                  let routerId = connectedToEdge(routerIds, workInfo);
                  if (routerId) {
                    this.connection
                      .sendMgmtQuery("GET-MGMT-NODES", routerId)
                      .then(
                        function (results) {
                          let response = results.response;
                          if (
                            Object.prototype.toString.call(response) ===
                            "[object Array]"
                          ) {
                            // special case of edge case:
                            // we are connected to an edge router that is connected to
                            // a router that is not connected to any other interior routers
                            if (response.length === 0) {
                              response = [routerId];
                            }
                            this.doget(response).then(
                              function (workInfo) {
                                finish.call(this, workInfo);
                              }.bind(this)
                            );
                          }
                        }.bind(this)
                      );
                  } else {
                    finish.call(this, workInfo);
                  }
                }.bind(this)
              );
            }
          }.bind(this),
          function (error) {
            reject(error);
          }
        );
      }.bind(this)
    );
  }
  doget(ids) {
    return new Promise(
      function (resolve) {
        let workInfo = {};
        for (var i = 0; i < ids.length; ++i) {
          workInfo[ids[i]] = {};
        }
        var gotResponse = function (nodeName, entity, response) {
          workInfo[nodeName][entity] = response;
          workInfo[nodeName][entity]["timestamp"] = new Date();
        };
        var q = d3.queue(this.connection.availableQeueuDepth());
        for (var id in workInfo) {
          for (var entity in this.entityAttribs) {
            q.defer(
              this.q_fetchNodeInfo.bind(this),
              id,
              entity,
              this.entityAttribs[entity],
              q,
              gotResponse
            );
          }
        }
        q.await(
          function () {
            // filter out nodes that have no connection info
            if (this.filtering) {
              for (var id in workInfo) {
                if (!workInfo[id].connection) {
                  this.flux = true;
                  delete workInfo[id];
                }
              }
            }
            resolve(workInfo);
          }.bind(this)
        );
      }.bind(this)
    );
  }

  onDone(result) {
    clearTimeout(this._getTimer);
    if (this.updating)
      this._getTimer = setTimeout(this.get.bind(this), this.updateInterval);
    this.executeUpdatedActions(result);
  }
  startUpdating(filter) {
    this.stopUpdating();
    this.updating = true;
    this.filtering = filter;
    this.get();
  }
  stopUpdating() {
    this.updating = false;
    if (this._getTimer) {
      clearTimeout(this._getTimer);
      this._getTimer = null;
    }
  }
  fetchEntity(node, entity, attrs, callback) {
    var results = {};
    var gotResponse = function (nodeName, dotentity, response) {
      results = response;
    };
    var q = d3.queue(this.connection.availableQeueuDepth());
    q.defer(
      this.q_fetchNodeInfo.bind(this),
      node,
      entity,
      attrs,
      q,
      gotResponse
    );
    q.await(function () {
      callback(node, entity, results);
    });
  }
  // called from d3.queue.defer so the last argument (callback) is supplied by d3
  q_fetchNodeInfo(nodeId, entity, attrs, q, heartbeat, callback) {
    this.getNodeInfo(nodeId, entity, attrs, q, function (
      nodeName,
      dotentity,
      response
    ) {
      heartbeat(nodeName, dotentity, response);
      callback(null);
    });
  }
  // get all the requested entities/attributes for a single router
  fetchEntities(node, entityAttribs, doneCallback, resultCallback) {
    var q = d3.queue(this.connection.availableQeueuDepth());
    var results = {};
    if (!resultCallback) {
      resultCallback = function (nodeName, dotentity, response) {
        if (!results[nodeName]) results[nodeName] = {};
        results[nodeName][dotentity] = response;
      };
    }
    var gotAResponse = function (nodeName, dotentity, response) {
      resultCallback(nodeName, dotentity, response);
    };
    if (Object.prototype.toString.call(entityAttribs) !== "[object Array]") {
      entityAttribs = [entityAttribs];
    }
    for (var i = 0; i < entityAttribs.length; ++i) {
      var ea = entityAttribs[i];
      q.defer(
        this.q_fetchNodeInfo.bind(this),
        node,
        ea.entity,
        ea.attrs || [],
        q,
        gotAResponse
      );
    }
    q.await(function () {
      doneCallback(results);
    });
  }
  // get all the requested entities for all known routers
  fetchAllEntities(entityAttribs, doneCallback, resultCallback) {
    var q = d3.queue(this.connection.availableQeueuDepth());
    var results = {};
    if (!resultCallback) {
      resultCallback = function (nodeName, dotentity, response) {
        if (!results[nodeName]) results[nodeName] = {};
        results[nodeName][dotentity] = response;
      };
    }
    var gotAResponse = function (nodeName, dotentity, response) {
      resultCallback(nodeName, dotentity, response);
    };
    if (Object.prototype.toString.call(entityAttribs) !== "[object Array]") {
      entityAttribs = [entityAttribs];
    }
    var nodes = Object.keys(this._nodeInfo);
    for (var n = 0; n < nodes.length; ++n) {
      for (var i = 0; i < entityAttribs.length; ++i) {
        var ea = entityAttribs[i];
        q.defer(
          this.q_fetchNodeInfo.bind(this),
          nodes[n],
          ea.entity,
          ea.attrs || [],
          q,
          gotAResponse
        );
      }
    }
    q.await(function () {
      doneCallback(results);
    });
  }
  // enusre all the topology nones have all these entities
  ensureAllEntities(entityAttribs, callback, extra) {
    this.ensureEntities(
      Object.keys(this._nodeInfo),
      entityAttribs,
      callback,
      extra
    );
  }
  // ensure these nodes have all these entities. don't fetch unless forced to
  ensureEntities(nodes, entityAttribs, callback, extra) {
    if (Object.prototype.toString.call(nodes) !== "[object Array]") {
      nodes = [nodes];
    }
    this.addUpdateEntities(entityAttribs);
    this.doget(nodes).then(
      function (results) {
        this.saveResults(results);
        callback(extra, results);
      }.bind(this)
    );
  }
  addNodeInfo(id, entity, values) {
    // save the results in the nodeInfo object
    if (id) {
      if (!(id in this._nodeInfo)) {
        this._nodeInfo[id] = {};
      }
      // copy the values to allow garbage collection
      this._nodeInfo[id][entity] = values;
      this._nodeInfo[id][entity]["timestamp"] = new Date();
    }
  }
  isLargeNetwork() {
    return Object.keys(this._nodeInfo).length >= 12;
  }
  getConnForLink(link) {
    // find the connection for this link
    var conns = this._nodeInfo[link.nodeId].connection;
    if (!conns) return {};
    var connIndex = conns.attributeNames.indexOf("identity");
    var linkCons = conns.results.filter(function (conn) {
      return conn[connIndex] === link.connectionId;
    });
    return utils.flatten(conns.attributeNames, linkCons[0]);
  }
  nodeNameList() {
    var nl = [];
    for (var id in this._nodeInfo) {
      nl.push(utils.nameFromId(id));
    }
    return nl.sort();
  }
  nodeIdList() {
    var nl = [];
    for (var id in this._nodeInfo) {
      //if (this._nodeInfo['connection'])
      nl.push(id);
    }
    return nl.sort();
  }
  nodeList() {
    var nl = [];
    for (var id in this._nodeInfo) {
      nl.push({
        name: utils.nameFromId(id),
        id: id
      });
    }
    return nl;
  }
  // d3.queue'd function to make a management query for entities/attributes
  q_ensureNodeInfo(nodeId, entity, attrs, q, callback) {
    this.getNodeInfo(
      nodeId,
      entity,
      attrs,
      q,
      function (nodeName, dotentity, response) {
        this.addNodeInfo(nodeName, dotentity, response);
        callback(null);
      }.bind(this)
    );
    return {
      abort: function () {
        delete this._nodeInfo[nodeId];
      }
    };
  }
  getSingelRouterNode(nodeName, attrs) {
    let node = {
      id: utils.nameFromId(nodeName),
      protocolVersion: 1,
      instance: 0,
      linkState: [],
      nextHop: '(self)',
      validOrigins: [],
      address: nodeName,
      routerLink: '',
      cost: 0,
      lastTopoChange: 0,
      index: 0,
      name: nodeName,
      identity: nodeName,
      type: 'org.apache.qpid.dispatch.router.node'
    };
    let result = [];
    if (attrs.length === 0) {
      attrs = Object.keys(node);
    }
    attrs.forEach(attr => {
      result.push(node[attr]);
    });
    return result;
  }

  getNodeInfo(nodeName, entity, attrs, q, callback) {
    const self = this;
    var timedOut = function (q) {
      q.abort();
    };
    var atimer = setTimeout(timedOut, this.timeout, q);
    this.connection.sendQuery(nodeName, entity, attrs).then(
      function (response) {
        clearTimeout(atimer);
        if (entity === 'router.node' &&
          response.response.results.length === 0 &&
          Object.keys(self._nodeInfo).length === 1) {
          response.response.results = [self.getSingelRouterNode(nodeName, attrs)];
        }
        callback(nodeName, entity, response.response);
      },
      function () {
        q.abort();
      }
    );
  }
  getMultipleNodeInfo(
    nodeNames,
    entity,
    attrs,
    callback,
    selectedNodeId,
    aggregate
  ) {
    var self = this;
    if (typeof aggregate === "undefined") aggregate = true;
    var responses = {};
    var gotNodesResult = function (nodeName, dotentity, response) {
      responses[nodeName] = response;
    };
    var q = d3.queue(this.connection.availableQeueuDepth());
    nodeNames.forEach(function (id) {
      q.defer(
        self.q_fetchNodeInfo.bind(self),
        id,
        entity,
        attrs,
        q,
        gotNodesResult
      );
    });
    q.await(function () {
      if (aggregate)
        self.aggregateNodeInfo(
          nodeNames,
          entity,
          selectedNodeId,
          responses,
          callback
        );
      else {
        callback(nodeNames, entity, responses);
      }
    });
  }
  quiesceLink(nodeId, name) {
    var attributes = {
      adminStatus: "disabled",
      name: name
    };
    return this.connection.sendMethod(
      nodeId,
      "router.link",
      attributes,
      "UPDATE"
    );
  }
  aggregateNodeInfo(nodeNames, entity, selectedNodeId, responses, callback) {
    // aggregate the responses
    var self = this;
    var newResponse = {};
    var thisNode = responses[selectedNodeId];
    newResponse.attributeNames = thisNode.attributeNames;
    newResponse.results = thisNode.results;
    newResponse.aggregates = [];
    // initialize the aggregates
    for (var i = 0; i < thisNode.results.length; ++i) {
      // there is a result for each unique entity found (ie addresses, links, etc.)
      var result = thisNode.results[i];
      var vals = [];
      // there is a val for each attribute in this entity
      result.forEach(function (val) {
        vals.push({
          sum: val,
          detail: []
        });
      });
      newResponse.aggregates.push(vals);
    }
    var nameIndex = thisNode.attributeNames.indexOf("name");
    var ent = self.connection.schema.entityTypes[entity];
    var ids = Object.keys(responses);
    ids.sort();
    ids.forEach(function (id) {
      var response = responses[id];
      var results = response.results;
      results.forEach(function (result) {
        // find the matching result in the aggregates
        var found = newResponse.aggregates.some(function (aggregate) {
          if (aggregate[nameIndex].sum === result[nameIndex]) {
            // result and aggregate are now the same record, add the graphable values
            newResponse.attributeNames.forEach(function (key, i) {
              if (ent.attributes[key] && ent.attributes[key].graph) {
                if (id != selectedNodeId) aggregate[i].sum += result[i];
              }
              aggregate[i].detail.push({
                node: utils.nameFromId(id) + ":",
                val: result[i]
              });
            });
            return true; // stop looping
          }
          return false; // continute looking for the aggregate record
        });
        if (!found) {
          // this attribute was not found in the aggregates yet
          // because it was not in the selectedNodeId's results
          var vals = [];
          result.forEach(function (val) {
            vals.push({
              sum: val,
              detail: [
                {
                  node: utils.nameFromId(id),
                  val: val
                }
              ]
            });
          });
          newResponse.aggregates.push(vals);
        }
      });
    });
    callback(nodeNames, entity, newResponse);
  }
}

export default Topology;
