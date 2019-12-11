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

import React, { Component } from "react";
import { Checkbox } from "@patternfly/react-core";
import AddressesComponent from "../common/addressesComponent";
import PropTypes from "prop-types";

class TrafficComponent extends Component {
  static propTypes = {
    dots: PropTypes.bool.isRequired,
    congestion: PropTypes.bool.isRequired,
    addressColors: PropTypes.object.isRequired,
    handleChangeTrafficAnimation: PropTypes.func.isRequired,
    handleChangeTrafficFlowAddress: PropTypes.func.isRequired,
    handleHoverAddress: PropTypes.func.isRequired
  };
  constructor(props) {
    super(props);
    this.state = {};
  }

  render() {
    return (
      <ul className="options">
        <li className="legend-sublist" id="traffic-address">
          <ul>
            <li id="traffic-dots">
              <Checkbox
                label="Message path by address"
                isChecked={this.props.dots}
                onChange={this.props.handleChangeTrafficAnimation}
                aria-label="show traffic by address"
                id="check-traffic-address"
                name="dots"
              />
            </li>
            {this.props.dots ? (
              <li id="traffic-dots-addresses">
                <AddressesComponent
                  addressColors={this.props.addressColors}
                  handleChangeAddress={this.props.handleChangeTrafficFlowAddress}
                  handleHoverAddress={this.props.handleHoverAddress}
                />
              </li>
            ) : (
              <React.Fragment />
            )}
          </ul>
          <ul>
            <li id="traffic-congestion">
              <Checkbox
                label="Link utilization"
                isChecked={this.props.congestion}
                onChange={this.props.handleChangeTrafficAnimation}
                aria-label="show connection utilization"
                id="check-traffic-congestion"
                name="congestion"
              />
            </li>
            <li
              id="traffic-congestion-svg"
              className={this.props.congestion ? "congestion" : "hidden"}
            >
              <svg
                xmlns="http://www.w3.org/2000/svg"
                version="1.1"
                preserveAspectRatio="xMidYMid meet"
                width="140"
                height="40"
              >
                <defs>
                  <linearGradient
                    xmlns="http://www.w3.org/2000/svg"
                    id="colorGradient"
                    gradientUnits="userSpaceOnUse"
                    x1="0%"
                    y1="0%"
                    x2="100%"
                    y2="0%"
                  >
                    <stop style={{ stopColor: "#999999", stopOpacity: 1 }} offset="0" />
                    <stop
                      style={{ stopColor: "#00FF00", stopOpacity: 1 }}
                      offset="0.333"
                    />
                    <stop
                      style={{ stopColor: "#FFA500", stopOpacity: 1 }}
                      offset="0.666"
                    />
                    <stop style={{ stopColor: "#FF0000", stopOpacity: 1 }} offset="1" />
                  </linearGradient>
                </defs>
                <g>
                  <rect
                    width="140"
                    height="20"
                    x="0"
                    y="0"
                    fill="url(#colorGradient)"
                  ></rect>
                  <text x="1" y="30" className="label">
                    Idle
                  </text>
                  <text x="106" y="30" className="label">
                    Busy
                  </text>
                </g>
              </svg>
            </li>
          </ul>
        </li>
      </ul>
    );
  }
}

export default TrafficComponent;
