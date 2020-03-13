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
        [{ entity: "connection", attrs: ["role", "container"] }, { entity: "router" }],
        nodes => {
          // we have all the data now in the nodes object
          let allRouterFields = [];
          const lastNode = Object.keys(nodes)[Object.keys(nodes).length - 1];
          for (let node in nodes) {
            const connections = this.service.utilities.flattenAll(nodes[node].connection);
            allRouterFields.push(this.routerFields(nodes, node, connections));
            // add edge routers
            const edgeIds = connections
              .filter(c => c.role === "edge")
              .map(c => this.service.utilities.idFromName(c.container, "_edge"));
            this.service.management.topology.fetchEntities(
              edgeIds,
              [
                { entity: "connection", attrs: ["role", "container"] },
                { entity: "router" }
              ],
              edges => {
                for (let edge in edges) {
                  const connections = this.service.utilities.flattenAll(
                    edges[edge].connection
                  );
                  allRouterFields.push(this.routerFields(edges, edge, connections));
                }
                if (node === lastNode) {
                  resolve({ data: allRouterFields, page, perPage });
                }
              }
            );
          }
        }
      );
    });
  };

  routerFields = (nodes, nodeId, connections) => {
    const routerData = nodes[nodeId].router;
    let connectionCount = 0;
    connections.forEach(connection => {
      if (connection.role !== "inter-router" && connection.role !== "edge")
        ++connectionCount;
    });
    let routerRow = {
      connections: connectionCount,
      nodeId,
      id: this.service.utilities.nameFromId(nodeId)
    };
    routerData.attributeNames.forEach((routerAttr, i) => {
      if (routerAttr !== "id") {
        routerRow[routerAttr] = routerData.results[0][i];
      }
    });
    return routerRow;
  };
}

export default RouterData;
