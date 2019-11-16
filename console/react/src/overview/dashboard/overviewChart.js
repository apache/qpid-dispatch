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
import { ChartArea, ChartGroup } from "@patternfly/react-charts";

class OverviewChart extends React.Component {
  constructor(props) {
    super(props);
    this.containerRef = React.createRef();
    this.state = {
      width: 0
    };
  }

  componentDidMount() {
    window.addEventListener("resize", this.handleResize);
    this.handleResize();
  }

  componentWillUnmount() {
    window.removeEventListener("resize", this.handleResize);
  }

  handleResize = () => {
    this.setState({ width: this.containerRef.current.clientWidth });
  };

  render() {
    const { width } = this.state;
    return (
      <div className="chart-container" ref={this.containerRef}>
        <div className="dashboard-chart">
          {width === 0 ? (
            <React.Fragment />
          ) : (
              <ChartGroup
                aria-label={this.props.ariaLabel}
                ariaDesc="overview chart"
                ariaTitle={this.props.title}
                height={100}
                padding={0}
                width={width}
                themeColor={this.props.color}
              >
                <ChartArea
                  data={this.props.data}
                  style={{
                    data: this.props.style
                  }}
                />
              </ChartGroup>
            )}
        </div>
        <div className="dashboard-stat">
          <div className="deliveries-stat">
            {this.props.data[this.props.data.length - 1].toLocaleString()}
          </div>
          <div className="deliveries-title">{this.props.title}</div>
        </div>
      </div>
    );
  }
}

export default OverviewChart;
