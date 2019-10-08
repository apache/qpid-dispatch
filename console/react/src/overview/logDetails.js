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
  DataList,
  DataListItem,
  DataListItemRow,
  DataListItemCells,
  DataListCell,
  DataListContent
} from "@patternfly/react-core";
import {
  Stack,
  StackItem,
  TextContent,
  Text,
  TextVariants,
  Breadcrumb,
  BreadcrumbItem
} from "@patternfly/react-core";

import { Redirect } from "react-router-dom";
import { dataMap } from "./entityData";

class LogDetails extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      redirect: false,
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
    if (!this.entity) {
      this.state.redirect = true;
    }
  }

  componentDidMount = () => {
    this.timer = setInterval(this.update, 5000);
    this.update();
  };

  componentWillUnmount = () => {
    if (this.timer) {
      clearInterval(this.timer);
    }
  };

  update = () => {};
  icap = s => s.charAt(0).toUpperCase() + s.slice(1);

  parentItem = () => {
    // otherwise return the 1st field
    return this.props.location.state.value;
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
          {" "}
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
                  {`Logs ${this.parentItem()} attributes`}
                </Text>
                <Text className="overview-loading" component={TextVariants.pre}>
                  {`Updated ${this.props.service.utilities.strDate(
                    this.state.lastUpdated
                  )}`}
                </Text>
              </TextContent>
            </StackItem>

            <StackItem className="overview-table">
              <DataList aria-label="Simple data list example">
                <DataListItem aria-labelledby="simple-item1">
                  <DataListItemRow>
                    <DataListItemCells
                      dataListCells={[
                        <DataListCell key="primary content">
                          <span id="simple-item1">Primary content</span>
                        </DataListCell>,
                        <DataListCell key="secondary content">
                          Secondary content
                        </DataListCell>
                      ]}
                    />
                  </DataListItemRow>
                </DataListItem>
                <DataListItem aria-labelledby="simple-item2">
                  <DataListItemRow>
                    <DataListItemCells
                      dataListCells={[
                        <DataListCell
                          isFilled={false}
                          key="secondary content fill"
                        >
                          <span id="simple-item2">
                            Secondary content (pf-m-no-fill)
                          </span>
                        </DataListCell>,
                        <DataListCell
                          isFilled={false}
                          alignRight
                          key="secondary content align"
                        >
                          Secondary content (pf-m-align-right pf-m-no-fill)
                        </DataListCell>
                      ]}
                    />
                  </DataListItemRow>
                </DataListItem>
              </DataList>
            </StackItem>
          </Stack>
        </PageSection>
      </React.Fragment>
    );
  }
}

export default LogDetails;
