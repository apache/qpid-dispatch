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
  TextVariants
} from "@patternfly/react-core";
import { Card, CardBody } from "@patternfly/react-core";

import OverviewTable from "./overviewTable";
import Updated from "../common/updated";

class OverviewPage extends React.Component {
  constructor(props) {
    super(props);
    this.state = { loading: false, lastUpdated: new Date() };
  }

  lastUpdated = lastUpdated => {
    this.setState({ lastUpdated });
  };
  render() {
    return (
      <PageSection
        variant={PageSectionVariants.light}
        className="overview-table-page"
        data-testid="overview-page"
      >
        <Stack>
          <StackItem className="overview-header">
            <TextContent>
              <Text className="overview-title" component={TextVariants.h1}>
                {this.props.service.utilities.entityFromProps(this.props)}
              </Text>
              <Updated
                service={this.props.service}
                lastUpdated={this.state.lastUpdated}
              />
            </TextContent>
          </StackItem>
          <StackItem className="overview-table">
            <Card>
              <CardBody>
                <OverviewTable {...this.props} lastUpdated={this.lastUpdated} />
              </CardBody>
            </Card>
          </StackItem>
        </Stack>
      </PageSection>
    );
  }
}

export default OverviewPage;
