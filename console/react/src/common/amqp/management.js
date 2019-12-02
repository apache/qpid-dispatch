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

import { ConnectionManager } from "./connection.js";
import Topology from "./topology.js";

export class Management {
  constructor(protocol, interval) {
    this.connection = new ConnectionManager(protocol);
    this.topology = new Topology(this.connection, interval);
  }
  getSchema(callback) {
    var self = this;
    return new Promise(function(resolve, reject) {
      if (self.connection.schema) {
        if (callback) callback(self.connection.schema);
        resolve(self.connection.schema);
        return;
      }
      self.connection.sendMgmtQuery("GET-SCHEMA").then(
        function(responseAndContext) {
          var response = responseAndContext.response;
          for (var entityName in response.entityTypes) {
            var entity = response.entityTypes[entityName];
            if (entity.deprecated) {
              // deprecated entity
              delete response.entityTypes[entityName];
            } else {
              for (var attributeName in entity.attributes) {
                var attribute = entity.attributes[attributeName];
                if (attribute.deprecated) {
                  // deprecated attribute
                  delete response.entityTypes[entityName].attributes[attributeName];
                }
              }
            }
          }
          self.connection.setSchema(response);
          if (callback) callback(response);
          resolve(response);
        },
        function(error) {
          if (callback) callback(error);
          reject(error);
        }
      );
    });
  }
  schema() {
    return this.connection.schema;
  }
}
