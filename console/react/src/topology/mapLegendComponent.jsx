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

class MapLegendComponent extends Component {
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

  handleColorChange = e => {
    this.props.handleUpdateMapColor(e.target.name, e.target.value);
  };

  render() {
    return (
      <ul className="map-legend">
        <li>
          <input
            id="areaColor"
            name="areaColor"
            type="color"
            value={this.props.areaColor}
            onChange={this.handleColorChange}
          />{" "}
          <label htmlFor="areaColor">Land</label>
        </li>
        <li>
          <input
            id="oceanColor"
            name="oceanColor"
            type="color"
            value={this.props.oceanColor}
            onChange={this.handleColorChange}
          />{" "}
          <label htmlFor="oceanColor">Ocean</label>
        </li>
      </ul>
    );
  }
}

export default MapLegendComponent;
