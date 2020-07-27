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

import { utils } from "../src/common/amqp/utilities";
import fetchResults from "./fetchEntities";
import nodeInfo from "./nodeInfo";
import schema from "./schema";

const methodResults = {
  context: {
    message: {
      application_properties: {
        statusCode: 200,
        statusDescription: "fake response was OK"
      }
    }
  }
};

export const mockService = ({ onSendMethod }) => {
  const cbSendMethod = onSendMethod ? onSendMethod : () => {};
  return {
    management: {
      schema: () => schema,
      connection: {
        sendMethod: () => {
          cbSendMethod();
          return Promise.resolve(methodResults);
        },
        is_connected: () => true,
        setReconnect: () => {},
        getReceiverAddress: () => 'amqp:/_topo/0/routerName/',
      },
      topology: {
        setUpdateEntities: () => {},
        ensureAllEntities: (stuff, cb) => cb(),
        ensureEntities: (foo, bar, cb) => cb(foo, fetchResults),
        startUpdating: () => new Promise(resolve => resolve()),
        stopUpdating: () => {},
        addChangedAction: () => {},
        delChangedAction: () => {},
        addUpdatedAction: () => {},
        delUpdatedAction: () => {},
        get: () => {
          return new Promise(resolve => {
            resolve();
          });
        },
        edgesPerRouter: () => {},
        edgeList: [],
        nodeIdList: () => [],
        fetchAllEntities: (stuff, cb) => {
          cb(fetchResults);
        },
        fetchEntity: (node, entity, attrs, cb) => cb(node, entity, {results: []}),
        fetchEntities: (foo, bar, cb) => cb(fetchResults),
        nodeInfo: () => nodeInfo,
        _nodeInfo: nodeInfo
      }
    },
    utilities: utils,
    connect: () => Promise.resolve(),
    schema,
    setHooks: () => {}
  };
};
