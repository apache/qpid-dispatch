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

import { sortable } from "@patternfly/react-table";
import OverviewTableBase from "./overviewTableBase";

class LinksTable extends OverviewTableBase {
  constructor(props) {
    super(props);
    this.fields = [
      { title: "Host", field: "name", transforms: [sortable] },
      { title: "Container", field: "container", transforms: [sortable] },
      { title: "Role", field: "role", transforms: [sortable] },
      { title: "Dir", field: "dir", transforms: [sortable] },
      { title: "Security", field: "security", transforms: [sortable] },
      {
        title: "Authentication",
        field: "authentication",
        transforms: [sortable]
      },
      { title: "Close", field: "close" }
    ];
  }
  doFetch = (page, perPage) => {
    return new Promise(resolve => {
      this.props.service.management.topology.fetchAllEntities(
        { entity: "connection" },
        nodes => {
          // we have all the data now in the nodes object
          let connectionFields = [];
          for (let node in nodes) {
            const response = nodes[node]["connection"];
            for (let i = 0; i < response.results.length; i++) {
              const result = response.results[i];
              const connection = this.props.service.utilities.flatten(
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
                else
                  sec = connection.sslProto + "(" + connection.sslCipher + ")";
              }

              let host = connection.host;
              let connField = {
                host: host,
                security: sec,
                authentication: auth,
                routerId: node,
                uid: host + connection.container + connection.identity
              };
              response.attributeNames.forEach(function(attribute, i) {
                connField[attribute] = result[i];
              });
              connectionFields.push(connField);
            }
          }
          resolve(this.slice(connectionFields, page, perPage));
        }
      );
    });
  };
}

export default LinksTable;
