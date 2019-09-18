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

class TrafficComponent extends Component {
  constructor(props) {
    super(props);
    this.state = {};
  }

  dotClicked = address => {
    this.props.handleChangeTrafficFlowAddress(
      address,
      !this.props.addresses[address]
    );
  };
  coloredDot = (address, i) => {
    return (
      <svg
        className="address-svg"
        id={`address-dot-${i}`}
        width="200"
        height="20"
      >
        <g
          transform="translate(10,10)"
          onClick={() => this.dotClicked(address)}
        >
          <circle r="10" fill={this.props.addressColors[address]} />
          {this.props.addresses[address] ? (
            <text x="-8" y="5" className="address-checkbox">
              &#xf00c;
            </text>
          ) : (
            ""
          )}
          <text x="20" y="5" className="label">
            {address}
          </text>
        </g>
      </svg>
    );
  };

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
            <li id="traffic-dots-addresses">
              <ul className={this.props.dots ? "addresses" : "hidden"}>
                {Object.keys(this.props.addresses).length === 0 ? (
                  <li key={`address-empty`}>There is no traffic</li>
                ) : (
                  Object.keys(this.props.addresses).map((address, i) => {
                    return (
                      <li key={`address-${i}`} className="legend-line">
                        {this.coloredDot(address, i)}
                      </li>
                    );
                  })
                )}
              </ul>
            </li>
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
                    id="gradienta1bEihLEHL"
                    gradientUnits="userSpaceOnUse"
                    x1="0%"
                    y1="0%"
                    x2="100%"
                    y2="0%"
                  >
                    <stop
                      style={{ stopColor: "#999999", stopOpacity: 1 }}
                      offset="0"
                    />
                    <stop
                      style={{ stopColor: "#00FF00", stopOpacity: 1 }}
                      offset="0.333"
                    />
                    <stop
                      style={{ stopColor: "#FFA500", stopOpacity: 1 }}
                      offset="0.666"
                    />
                    <stop
                      style={{ stopColor: "#FF0000", stopOpacity: 1 }}
                      offset="1"
                    />
                  </linearGradient>
                </defs>
                <g>
                  <rect
                    width="140"
                    height="20"
                    x="0"
                    y="0"
                    fill="url(#gradienta1bEihLEHL)"
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
