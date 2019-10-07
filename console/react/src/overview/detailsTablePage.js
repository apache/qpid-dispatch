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
import { Redirect } from "react-router-dom";
import { dataMap } from "./entityData";

class DetailTablesPage extends React.Component {
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
      lastUpdated: new Date()
    };
    // if we get to this page and we don't have a props.location.state.entity
    // then redirect back to the dashboard.
    // this can happen if we get here from a bookmark or browser refresh
    this.entity =
      this.props &&
      this.props.location &&
      this.props.location.state &&
      this.props.location.state.entity;
    if (!dataMap[this.entity]) {
      this.state.redirect = true;
    } else {
      this.dataSource = new dataMap[this.entity](this.props.service);
    }
  }

  componentDidMount = () => {
    this.props.service.management.getSchema().then(schema => {
      this.schema = schema;
      this.timer = setInterval(this.update, 5000);
      this.update();
    });
  };

  componentWillUnmount = () => {
    if (this.timer) {
      clearInterval(this.timer);
    }
  };

  update = () => {
    this.mapRows().then(
      rows => {
        this.setState({ rows, lastUpdated: new Date() });
      },
      error => {
        console.log(`detailsTablePage: ${error}`);
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
        .fetchRecord(this.props.location.state.currentRecord, this.schema)
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
    // if we have a specific field that should be used
    // as the record's title, return it
    if (this.dataSource.detailField) {
      return this.props.location.state.currentRecord[
        this.dataSource.detailField
      ];
    }
    // otherwise return the 1st field
    return this.props.location.state.value;
  };

  breadcrumbSelected = () => {
    this.setState({
      redirect: true,
      redirectPath: `/overview/${this.entity}`,
      redirectState: this.props.location.state
    });
  };

  render() {
    if (this.state.redirect) {
      return (
        <Redirect
          to={{
            pathname: this.state.redirectPath,
            state: this.state.redirectState
          }}
        />
      );
    }

    return (
      <React.Fragment>
        <PageSection
          variant={PageSectionVariants.light}
          className="overview-table-page"
        >
          <Stack>
            <StackItem className="overview-header details">
              <Breadcrumb>
                <BreadcrumbItem
                  className="link-button"
                  onClick={() =>
                    this.breadcrumbSelected(`/overview/${this.entity}`)
                  }
                >
                  {this.icap(this.entity)}
                </BreadcrumbItem>
                <BreadcrumbItem isActive>{this.parentItem()}</BreadcrumbItem>
              </Breadcrumb>

              <TextContent>
                <Text className="overview-title" component={TextVariants.h1}>
                  {`${
                    this.dataSource.detailName
                  } ${this.parentItem()} attributes`}
                </Text>
                <Text className="overview-loading" component={TextVariants.pre}>
                  {`Updated ${this.props.service.utilities.strDate(
                    this.state.lastUpdated
                  )}`}
                </Text>
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

export default DetailTablesPage;
