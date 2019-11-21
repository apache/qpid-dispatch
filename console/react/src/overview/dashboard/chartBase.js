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
import * as d3 from "d3";

import OverviewChart from "./overviewChart";

class ChartBase extends React.Component {
  constructor(props) {
    super(props);
    this.title = "Override me";
    this.ariaLabel = "base-chart";
    this.isRate = false;
    this.style = { fill: "#EBAEBA", fillOpacity: 1, stroke: "#EAEAEA" };
    this.state = {
      data: this.props.chartData.data(this.props.period)
    };
  }

  componentDidMount = () => {
    this.timer = setInterval(this.setData, 1000);
  };

  componentWillUnmount = () => {
    if (this.timer) {
      clearInterval(this.timer);
    }
  };

  setData = () => {
    this.setState({ data: this.props.chartData.data(this.props.period) });
  };

  setStyle = (color, opacity) => {
    this.style = {
      fill: color,
      fillOpacity: opacity || 1,
      stroke: d3.rgb(color).darker(2)
    };
  };

  render() {
    return (
      <OverviewChart
        ariaLabel={this.ariaLabel}
        data={this.state.data}
        title={this.title}
        style={this.style}
      />
    );
  }
}

export default ChartBase;
