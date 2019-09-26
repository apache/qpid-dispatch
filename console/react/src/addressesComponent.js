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

class AddressesComponent extends Component {
  constructor(props) {
    super(props);
    this.state = {};
  }

  dotClicked = address => {
    this.props.handleChangeAddress(address, !this.props.addresses[address]);
  };

  dotHover = (address, over) => {
    if (this.props.handleHoverAddress) {
      this.props.handleHoverAddress(address, over);
    }
  };

  coloredDot = (address, i) => {
    return (
      <svg
        className="address-svg"
        id={`address-dot-${i}`}
        width="200"
        height="20"
        title="Click to show/hide this address"
      >
        <g
          transform="translate(10,10)"
          onClick={() => this.dotClicked(address)}
          onMouseOver={() => this.dotHover(address, true)}
          onMouseOut={() => this.dotHover(address, false)}
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
      <ul className="addresses">
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
    );
  }
}

export default AddressesComponent;
