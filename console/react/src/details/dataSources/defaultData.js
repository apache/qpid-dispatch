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

import React from "react";
import { utils } from "../../common/amqp/utilities";
import DeleteEntity from "../deleteEntity";
import UpdateEntity from "../updateEntity";
import CreateEntity from "../createEntity";

class DefaultData {
  constructor(service, schema) {
    this.service = service;
    this.schema = schema;
    this.actionMap = {
      DELETE: DeleteEntity,
      UPDATE: UpdateEntity,
      CREATE: CreateEntity
    };
  }

  schemaAttributes = entity => this.schema.entityTypes[entity].attributes;
  schemaOperations = entity => this.schema.entityTypes[entity].operations;

  // emit a single button/component
  entityAction = ({ component: Component, props, record, click, i, asButton }) => (
    <Component
      key={`action-${i}`}
      record={record}
      notifyClick={click}
      {...props}
      asButton={asButton}
    />
  );

  // action buttons for the detailsTablePage for a single record
  detailActions = (entity, props, record, click) => (
    <>
      {this.actions(entity)
        .filter(action => action !== "CREATE")
        .map((action, i) =>
          this.entityAction({
            component: this.actionMap[action],
            props,
            record,
            click,
            i,
            asButton: true
          })
        )}
    </>
  );

  // called by detailsTablePage to display a single record
  fetchRecord = (currentRecord, schema, entity) => {
    return new Promise(resolve => {
      this.service.management.topology.fetchEntities(
        currentRecord.routerId,
        [{ entity: entity }],
        data => {
          const record = data[currentRecord.routerId][entity];
          const identityIndex = record.attributeNames.indexOf("identity");
          const result = record.results.find(
            r => r[identityIndex] === currentRecord.identity
          );
          let object = this.service.utilities.flatten(record.attributeNames, result);
          object = this.service.utilities.formatAttributes(
            object,
            schema.entityTypes[entity]
          );
          resolve(object);
        }
      );
    });
  };

  // return a list of operations allowed for this entity
  actions = entity =>
    this.schema.entityTypes[entity].operations.filter(action => action !== "READ");

  // action button for the entityListTable
  actionButton = ({ action, props, click, record, i, asButton }) =>
    this.entityAction({
      component: this.actionMap[action],
      props,
      click,
      record,
      i,
      asButton
    });

  // actions menu on entityListTable for each record
  actionMenuItems = (entity, click) => {
    const actions = this.actions(entity).filter(action => action !== "CREATE");
    return actions.map(action => ({
      title: action,
      onClick: (event, rowId, rowData, extra) => {
        click({ action, entity, event, rowId, rowData, extra });
      }
    }));
  };

  // called by entityListTable to get the list of records
  doFetch = (page, perPage, routerId, entity) => {
    return new Promise(resolve => {
      this.service.management.topology.fetchEntities(routerId, { entity }, results => {
        const data = utils.flattenAll(results[routerId][entity], f => {
          f.routerId = routerId;
          return f;
        });
        resolve({ data, page, perPage });
      });
    });
  };

  validate = record => {
    return { validated: true };
  };
}

export default DefaultData;
