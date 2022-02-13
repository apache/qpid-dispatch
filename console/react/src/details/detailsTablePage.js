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
import {
  Stack,
  StackItem,
  TextContent,
  Text,
  TextVariants,
  Breadcrumb,
  BreadcrumbItem
} from "@patternfly/react-core";

import {
  cellWidth,
  Table,
  TableHeader,
  TableBody,
  TableVariant
} from "@patternfly/react-table";
import { Card, CardBody } from "@patternfly/react-core";
import { Navigate } from "react-router-dom";
import { dataMap } from "../overview/entityData";
import { dataMap as detailsDataMap, defaultData } from "./entityData";
import Updated from "../common/updated";

class DetailsTablePage extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      columns: [
        { title: "Attribute", transforms: [cellWidth(20)] },
        {
          title: "Value",
          transforms: [cellWidth("max")],
          props: { className: "pf-u-text-align-left" }
        }
      ],
      rows: [],
      redirect: false,
      redirectState: { page: 1 },
      redirectPath: "/dashboard",
      lastUpdated: new Date(),
    };
    // if we get to this page and we don't have a props.location.state.entity
    // then redirect back to the dashboard.
    // this can happen if we get here from a bookmark or browser refresh
    this.entity =
      this.props.entity ||
      (this.props &&
        this.props.location &&
        this.props.location.state &&
        this.props.location.state.entity);

    if (!this.entity) {
      this.state.redirect = true;
    } else {
      if (this.props.details) {
        this.dataSource = !detailsDataMap[this.entity]
          ? new defaultData(this.props.service, this.props.schema)
          : new detailsDataMap[this.entity](this.props.service, this.props.schema);
      } else {
        this.dataSource = new dataMap[this.entity](this.props.service, this.props.schema);
      }
    }
  }

  componentDidMount = () => {
    this.timer = setInterval(this.update, 5000);
    this.update();
  };

  componentWillUnmount = () => {
    this.unmounted = true;
    if (this.timer) {
      clearInterval(this.timer);
    }
  };

  locationState = () => {
    return this.props.details ? this.props.locationState : this.props.location.state;
  };

  update = () => {
    this.mapRows().then(
      rows => {
        if (!this.unmounted)
          this.setState({ rows, lastUpdated: new Date() }, () => {
            if (this.props.details) {
              this.props.lastUpdated(this.state.lastUpdated);
            }
          });
      },
      error => {
        console.log(`detailsTablePage: ${error}`);
        if (this.timer) clearInterval(this.timer);
      }
    );
  };

  toString = val => {
    return val === null ? "" : String(val);
  };

  mapRows = () => {
    return new Promise((resolve, reject) => {
      const rows = [];
      if (!this.dataSource) {
        reject("no data source");
      }
      this.dataSource
        .fetchRecord(this.locationState().currentRecord, this.props.schema, this.entity)
        .then(data => {
          for (const attribute in data) {
            if (
              !this.dataSource.hideFields ||
              this.dataSource.hideFields.indexOf(attribute) === -1
            ) {
              rows.push({
                cells: [attribute, this.toString(data[attribute])]
              });
            }
          }
          resolve(rows);
        });
    });
  };

  icap = s => s.charAt(0).toUpperCase() + s.slice(1);

  parentItem = () => {
    if (this.locationState().currentRecord.name)
      return this.locationState().currentRecord.name;
    return `${this.entity}/${this.locationState().currentRecord.identity}`;
  };

  breadcrumbSelected = () => {
    if (this.props.details) {
      this.props.handleSelectEntity(this.entity);
    } else {
      this.setState({
        redirect: true,
        redirectPath: `/overview/${this.entity}`,
        redirectState: this.locationState()
      });
    }
  };

  handleActionClicked = (action, record) => {
    this.props.handleEntityAction(action, record);
  };

  render() {
    if (this.state.redirect) {
      return (
        <Navigate
          to={{
            pathname: this.state.redirectPath,
            state: this.state.redirectState
          }}
        />
      );
    }

    const actionsButtons = () => {
      return this.dataSource.detailActions(
        this.entity,
        this.props,
        this.locationState().currentRecord,
        event => this.handleActionClicked(event, this.locationState().currentRecord)
      );
    };

    return (
      <React.Fragment>
        <PageSection variant={PageSectionVariants.light} className="overview-table-page">
          <Stack>
            <StackItem className="overview-header details">
              <Breadcrumb>
                <BreadcrumbItem className="link-button" onClick={this.breadcrumbSelected}>
                  {this.icap(this.entity)}
                </BreadcrumbItem>
              </Breadcrumb>

              <TextContent className="details-table-header">
                <Text
                  data-testid={`detail-for-${this.parentItem()}`}
                  className="overview-title"
                  component={TextVariants.h1}
                >
                  {this.parentItem()}
                </Text>
                {!this.props.details && (
                  <Updated
                    service={this.props.service}
                    lastUpdated={this.state.lastUpdated}
                  />
                )}
                {this.props.details && actionsButtons()}
              </TextContent>
            </StackItem>
            <StackItem className="overview-table">
              <Card>
                <CardBody>
                  <Table
                    cells={this.state.columns}
                    rows={this.state.rows}
                    variant={TableVariant.compact}
                    aria-label={this.entity}
                  >
                    <TableHeader />
                    <TableBody />
                  </Table>
                </CardBody>
              </Card>
            </StackItem>
          </Stack>
        </PageSection>
      </React.Fragment>
    );
  }
}

export default DetailsTablePage;
