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

// eslint-disable-next-line import/no-anonymous-default-export
export default {
  "amqp:/_topo/0/A/$management": {
    connection: {
      attributeNames: [
        "name",
        "identity",
        "host",
        "role",
        "dir",
        "container",
        "sasl",
        "isAuthenticated",
        "user",
        "isEncrypted",
        "sslProto",
        "sslCipher",
        "properties",
        "sslSsf",
        "tenant",
        "type",
        "ssl",
        "opened",
        "active",
        "adminStatus",
        "operStatus"
      ],
      results: [
        [
          "connection/127.0.0.1:60244",
          "1",
          "127.0.0.1:60244",
          "inter-router",
          "in",
          "B",
          "ANONYMOUS",
          true,
          "anonymous",
          false,
          null,
          null,
          {
            product: "qpid-dispatch-router",
            version: "1.9.0-SNAPSHOT",
            "qd.conn-id": 1
          },
          0,
          null,
          "org.apache.qpid.dispatch.connection",
          false,
          true,
          true,
          "enabled",
          "up"
        ],
        [
          "connection/127.0.0.1:53474",
          "2",
          "127.0.0.1:53474",
          "normal",
          "in",
          "87c75b00-3eae-4289-b2ef-7faae85682b3",
          "ANONYMOUS",
          true,
          "anonymous",
          false,
          null,
          null,
          {},
          0,
          null,
          "org.apache.qpid.dispatch.connection",
          false,
          true,
          true,
          "enabled",
          "up"
        ],
        [
          "connection/::1",
          "8",
          "::1",
          "normal",
          "in",
          "9fdbf671-8845-ac4c-9912-21cad048e1c2",
          null,
          false,
          "anonymous",
          false,
          null,
          null,
          {
            console_identifier: "Dispatch console"
          },
          0,
          null,
          "org.apache.qpid.dispatch.connection",
          false,
          true,
          true,
          "enabled",
          "up"
        ]
      ],
      timestamp: "2019-11-16T14:25:47.289Z"
    }
  },
  "amqp:/_topo/0/B/$management": {
    connection: {
      attributeNames: [
        "name",
        "identity",
        "host",
        "role",
        "dir",
        "container",
        "sasl",
        "isAuthenticated",
        "user",
        "isEncrypted",
        "sslProto",
        "sslCipher",
        "properties",
        "sslSsf",
        "tenant",
        "type",
        "ssl",
        "opened",
        "active",
        "adminStatus",
        "operStatus"
      ],
      results: [
        [
          "connection/0.0.0.0:2000",
          "1",
          "0.0.0.0:2000",
          "inter-router",
          "out",
          "A",
          "ANONYMOUS",
          true,
          null,
          false,
          null,
          null,
          {
            product: "qpid-dispatch-router",
            version: "1.9.0-SNAPSHOT",
            "qd.conn-id": 1
          },
          0,
          null,
          "org.apache.qpid.dispatch.connection",
          false,
          true,
          true,
          "enabled",
          "up"
        ],
        [
          "connection/127.0.0.1:34780",
          "2",
          "127.0.0.1:34780",
          "normal",
          "in",
          "af560918-3c35-4bf4-a9da-eef5a11ff7ec",
          "ANONYMOUS",
          true,
          "anonymous",
          false,
          null,
          null,
          {},
          0,
          null,
          "org.apache.qpid.dispatch.connection",
          false,
          true,
          true,
          "enabled",
          "up"
        ]
      ],
      timestamp: "2019-11-16T14:25:47.290Z"
    }
  }
};
