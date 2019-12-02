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

class RouterData {
  constructor(service) {
    this.service = service;
    this.fields = [
      { title: "Router", field: "name" },
      { title: "Mode", field: "mode" },
      {
        title: "Addresses",
        field: "addrCount",
        numeric: true
      },
      {
        title: "Links",
        field: "linkCount",
        numeric: true
      },
      {
        title: "External connections",
        field: "connections",
        numeric: true
      }
    ];
    this.detailEntity = "router";
    this.detailName = "Router";
  }

  fetchRecord = (currentRecord, schema) => {
    return new Promise(resolve => {
      this.service.management.topology.fetchEntities(
        currentRecord.nodeId,
        [{ entity: "router" }],
        results => {
          const record = results[currentRecord.nodeId].router;
          let router = this.service.utilities.flatten(
            record.attributeNames,
            record.results[0]
          );
          router = this.service.utilities.formatAttributes(
            router,
            schema.entityTypes.router
          );
          resolve(router);
        }
      );
    });
  };

  doFetch = (page, perPage) => {
    return new Promise(resolve => {
      this.service.management.topology.fetchAllEntities(
        [{ entity: "connection", attrs: ["role"] }, { entity: "router" }],
        nodes => {
          // we have all the data now in the nodes object
          let allRouterFields = [];
          for (let node in nodes) {
            let connections = 0;
            for (let i = 0; i < nodes[node]["connection"].results.length; ++i) {
              // we only requested "role" so it will be at results[0]
              if (nodes[node]["connection"].results[i][0] !== "inter-router")
                ++connections;
            }
            let routerRow = {
              connections,
              nodeId: node,
              id: this.service.utilities.nameFromId(node)
            };
            nodes[node]["router"].attributeNames.forEach((routerAttr, i) => {
              if (routerAttr !== "id") {
                routerRow[routerAttr] = nodes[node]["router"].results[0][i];
              }
            });
            allRouterFields.push(routerRow);
          }
          resolve({ data: allRouterFields, page, perPage });
        }
      );
    });
  };
}

export default RouterData;
