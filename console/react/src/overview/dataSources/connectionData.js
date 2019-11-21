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

import ConnectionClose from "../../common/connectionClose";

class ConnectionData {
  constructor(service) {
    this.service = service;
    this.fields = [
      {
        title: "Host",
        field: "name"
      },
      { title: "Container", field: "container" },
      { title: "Role", field: "role" },
      { title: "Dir", field: "dir" },
      { title: "Security", field: "security" },
      {
        title: "Authentication",
        field: "authentication"
      },
      {
        title: "",
        noSort: true,
        formatter: ConnectionClose
      }
    ];
    this.detailEntity = "connection";
    this.detailName = "Connection";
  }

  fetchRecord = (currentRecord, schema) => {
    return new Promise(resolve => {
      this.service.management.topology.fetchEntities(
        currentRecord.nodeId,
        [{ entity: "connection" }],
        data => {
          const record = data[currentRecord.nodeId]["connection"];
          const identityIndex = record.attributeNames.indexOf("identity");
          const result = record.results.find(
            r => r[identityIndex] === currentRecord.identity
          );
          let connection = this.service.utilities.flatten(record.attributeNames, result);
          connection = this.service.utilities.formatAttributes(
            connection,
            schema.entityTypes["connection"]
          );
          resolve(connection);
        }
      );
    });
  };

  doFetch = (page, perPage) => {
    return new Promise(resolve => {
      this.service.management.topology.fetchAllEntities(
        { entity: "connection" },
        nodes => {
          // we have all the data now in the nodes object
          let connectionFields = [];
          for (let node in nodes) {
            const response = nodes[node]["connection"];
            for (let i = 0; i < response.results.length; i++) {
              const result = response.results[i];
              const connection = this.service.utilities.flatten(
                response.attributeNames,
                result
              );
              let auth = "no_auth";
              let sasl = connection.sasl;
              if (connection.isAuthenticated) {
                auth = sasl;
                if (sasl === "ANONYMOUS") auth = "anonymous-user";
                else {
                  if (sasl === "GSSAPI") sasl = "Kerberos";
                  if (sasl === "EXTERNAL") sasl = "x.509";
                  auth = connection.user + "(" + connection.sslCipher + ")";
                }
              }

              let sec = "no-security";
              if (connection.isEncrypted) {
                if (sasl === "GSSAPI") sec = "Kerberos";
                else sec = connection.sslProto + "(" + connection.sslCipher + ")";
              }

              let host = connection.host;
              let connField = {
                host: host,
                security: sec,
                authentication: auth,
                nodeId: node,
                uid: host + connection.container + connection.identity,
                identity: connection.identity
              };
              response.attributeNames.forEach(function(attribute, i) {
                connField[attribute] = result[i];
              });
              connectionFields.push(connField);
            }
          }
          resolve({ data: connectionFields, page, perPage });
        }
      );
    });
  };
}

export default ConnectionData;
