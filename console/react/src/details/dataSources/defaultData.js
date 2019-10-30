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

import { utils } from "../../amqp/utilities";

class DefaultData {
  constructor(service, schema) {
    this.service = service;
    this.schema = schema;
  }

  hasType = () => {
    return false;
  };

  actions = entity => {
    return this.schema.entityTypes[entity].operations.filter(
      action => action !== "READ"
    );
  };

  fetchRecord = (currentRecord, schema) => {
    return new Promise(resolve => {
      resolve(null);
    });
  };

  doFetch = (page, perPage, routerId, entity) => {
    return new Promise(resolve => {
      this.service.management.topology.fetchEntities(
        routerId,
        { entity },
        results => {
          const data = utils.flattenAll(results[routerId][entity]);
          resolve({ data, page, perPage });
        }
      );
    });
  };
}

export default DefaultData;
