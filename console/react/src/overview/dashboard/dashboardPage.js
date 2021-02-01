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
import { Card, CardTitle, CardBody } from "@patternfly/react-core";
import { Split, SplitItem } from "@patternfly/react-core";
import ThroughputChart from "./throughputChart";
import InflightChart from "./inflightChart";
import ActiveAddressesCard from "./activeAddressesCard";
import DelayedDeliveriesCard from "./delayedDeliveriesCard";

class DashboardPage extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      timePeriod: 60,
    };
  }

  setTimePeriod = timePeriod => {
    this.setState({ timePeriod });
  };

  timePeriodString = () => (this.state.timePeriod === 60 ? "minute" : "hour");

  render() {
    return (
      <PageSection
        variant={PageSectionVariants.light}
        className="overview-charts-page"
        aria-label="dashboard-page"
      >
        <Stack hasGutter="md">
          <StackItem>
            <Card>
              <CardTitle>
                <div className="dashboard-header">
                  <div>Router network statistics</div>
                  <div className="duration-tabs">
                    <nav className="pf-c-nav" aria-label="Local">
                      <ul
                        className="pf-c-nav__tertiary-list"
                        style={{ display: "inline-flex" }}
                      >
                        <li
                          style={{ marginRight: "32px" }}
                          onClick={() => this.setTimePeriod(60)}
                          className={`pf-c-nav__item ${
                            this.state.timePeriod === 60 ? "selected" : ""
                          }`}
                        >
                          Minute
                        </li>
                        <li
                          onClick={() => this.setTimePeriod(60 * 60)}
                          className={`pf-c-nav__item ${
                            this.state.timePeriod === 60 ? "" : "selected"
                          }`}
                        >
                          Hour
                        </li>
                      </ul>
                    </nav>
                  </div>
                </div>
                <div className="time-period">For the past {this.timePeriodString()}</div>
              </CardTitle>
              <CardBody>
                <ThroughputChart
                  period={this.state.timePeriod}
                  chartData={this.props.throughputChartData}
                />
                <InflightChart
                  period={this.state.timePeriod}
                  chartData={this.props.inflightChartData}
                />
              </CardBody>
            </Card>
          </StackItem>
          <StackItem>
            <Split hasGutter="md">
              <SplitItem id="activeAddresses">
                <ActiveAddressesCard service={this.props.service} />
              </SplitItem>
              <SplitItem id="delayedDeliveries" isFilled>
                <DelayedDeliveriesCard {...this.props} service={this.props.service} />
              </SplitItem>
            </Split>
          </StackItem>
        </Stack>
      </PageSection>
    );
  }
}

export default DashboardPage;
