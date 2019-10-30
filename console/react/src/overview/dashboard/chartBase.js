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
    this.state = {
      rates: []
    };
    this.rawData = [];
    this.rateStorage = {};
    this.initialized = false;
    for (let i = 0; i < 60 * 60; i++) {
      this.state.rates.push(0);
    }
    this.title = "Override me";
    this.isRate = false;
  }

  componentDidMount = () => {
    this.mounted = true;
    this.timer = setInterval(this.updateData, 1000);
  };

  componentWillUnmount = () => {
    this.mounted = false;
    clearInterval(this.timer);
  };

  setStyle = (color, opacity) => {
    this.style = {
      fill: color,
      fillOpacity: opacity || 1,
      stroke: d3.rgb(color).darker(2)
    };
  };
  updateData = () => {
    console.log("updateData should be overridden");
  };

  init = datum => {
    for (let i = 0; i < 60 * 60; i++) {
      this.rawData.push(datum);
    }
    this.initialized = true;
  };

  addData = datum => {
    if (!this.initialized) {
      this.init(datum);
    }
    if (!this.mounted) return;
    const { rates } = this.state;
    this.rawData.push(datum);
    this.rawData.splice(0, 1);
    if (this.isRate) {
      // get the average rate of change for the last three values
      const avg = this.props.service.utilities.rates(
        { val: datum },
        ["val"],
        this.rateStorage,
        "val",
        3
      );
      datum = Math.round(avg.val);
    }
    rates.push(datum);
    rates.splice(0, 1);
    this.setState({ rates });
  };

  data = () => {
    const start = Math.max(this.state.rates.length - this.props.period, 0);
    const end = this.state.rates.length - 1;
    return this.state.rates.slice(start, end);
  };

  render() {
    return (
      <OverviewChart data={this.data()} title={this.title} style={this.style} />
    );
  }
}

export default ChartBase;
