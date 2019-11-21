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
import { PageSection, PageSectionVariants } from "@patternfly/react-core";
import { Stack, StackItem } from "@patternfly/react-core";
import { Split, SplitItem } from "@patternfly/react-core";

import DetailsTablePage from "./detailsTablePage";
import UpdateTablePage from "./updateTablePage";
import CreateTablePage from "./createTablePage";
import EntityListTable from "./entityListTable";
import EntityList from "./entityList";
import RouterSelect from "./routerSelect";
import Updated from "../common/updated";

class EntitiesPage extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      loading: false,
      lastUpdated: new Date(),
      entity: null,
      routerId: null,
      showTable: "entities"
    };
    this.schema = this.props.service.management.schema();
  }

  lastUpdated = lastUpdated => {
    this.setState({ lastUpdated });
  };

  // called from entityList to change entity summary
  handleSwitchEntity = entity => {
    if (this.listTableRef) this.listTableRef.reset();
    this.setState({ entity, showTable: "entities", detailsState: {} });
  };

  // called from breadcrumb on detailsTablePage to return to current entity summary
  handleSelectEntity = entity => {
    this.setState({ entity, showTable: "entities" });
  };

  handleEntityAction = (action, record) => {
    if (action === "Done") action = "entities";
    this.setState({
      actionState: {
        currentRecord: record,
        entity: this.props.entity
      },
      showTable: action
    });
  };

  handleActionCancel = props => {
    const { detailsState } = this.state;
    const { page, sortBy, filterBy, perPage } = detailsState;
    const extraInfo = { rowData: { data: props.locationState.currentRecord } };
    if (!props.locationState.currentRecord) {
      this.handleSwitchEntity(this.state.entity);
    } else {
      this.handleDetailClick(props.locationState.currentRecord.name, extraInfo, {
        page,
        sortBy,
        filterBy,
        perPage
      });
    }
  };

  handleRouterSelected = routerId => {
    this.setState({ routerId, showTable: "entities" });
  };

  // clicked on 1st column in the entityTable
  // show the details page for this record
  handleDetailClick = (value, extraInfo, stateInfo) => {
    // pass along the current state of the entity table
    // so we can restore it if the breadcrumb on the details page is clicked
    this.setState({
      detailsState: {
        value: value,
        currentRecord: extraInfo.rowData.data,
        entity: this.props.entity,
        page: stateInfo.page,
        sortBy: stateInfo.sortBy,
        filterBy: stateInfo.filterBy,
        perPage: stateInfo.perPage,
        property: extraInfo.property
      },
      showTable: "details"
    });
  };

  render() {
    const TABLE = this.state.showTable.toUpperCase();
    const entityTable = () => {
      if (this.state.entity) {
        if (TABLE === "ENTITIES") {
          return (
            <EntityListTable
              ref={el => (this.listTableRef = el)}
              {...this.props}
              entity={this.state.entity}
              schema={this.schema}
              routerId={this.state.routerId}
              lastUpdated={this.lastUpdated}
              handleDetailClick={this.handleDetailClick}
              detailsState={this.state.detailsState}
              handleEntityAction={this.handleEntityAction}
            />
          );
        } else if (TABLE === "DETAILS") {
          return (
            <DetailsTablePage
              details={true}
              locationState={this.state.detailsState}
              entity={this.state.entity}
              {...this.props}
              lastUpdated={this.lastUpdated}
              schema={this.schema}
              handleSelectEntity={this.handleSelectEntity}
              handleEntityAction={this.handleEntityAction}
            />
          );
        } else if (TABLE === "UPDATE") {
          return (
            <UpdateTablePage
              entity={this.state.entity}
              {...this.props}
              schema={this.schema}
              locationState={this.state.actionState}
              handleSelectEntity={this.handleSelectEntity}
              handleActionCancel={this.handleActionCancel}
              handleEntityAction={this.handleEntityAction}
            />
          );
        } else if (TABLE === "CREATE") {
          return (
            <CreateTablePage
              entity={this.state.entity}
              routerId={this.state.routerId}
              {...this.props}
              schema={this.schema}
              locationState={this.state.actionState}
              handleSelectEntity={this.handleSelectEntity}
              handleActionCancel={this.handleActionCancel}
            />
          );
        }
      } else {
        return null;
      }
    };

    return (
      <PageSection variant={PageSectionVariants.light} className="details-table-page">
        <Stack>
          <StackItem className="details-header">
            <Split>
              <SplitItem isFilled className="split-left">
                <span className="prompt">Router</span>{" "}
                <RouterSelect
                  service={this.props.service}
                  handleRouterSelected={this.handleRouterSelected}
                />
              </SplitItem>
              <SplitItem>
                <Updated
                  service={this.props.service}
                  lastUpdated={this.state.lastUpdated}
                />
              </SplitItem>
            </Split>
          </StackItem>
          <StackItem className="details-table">
            <Split>
              <SplitItem id="entityList">
                <EntityList
                  schema={this.schema}
                  handleSelectEntity={this.handleSwitchEntity}
                />
              </SplitItem>
              <SplitItem isFilled>{entityTable()}</SplitItem>
            </Split>
          </StackItem>
        </Stack>
      </PageSection>
    );
  }
}

export default EntitiesPage;
