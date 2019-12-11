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
import * as d3 from "d3";
import PropTypes from "prop-types";
const FA = require("react-fontawesome");

class AddressesComponent extends Component {
  static propTypes = {
    handleChangeAddress: PropTypes.func.isRequired,
    handleHoverAddress: PropTypes.func.isRequired,
    addressColors: PropTypes.object.isRequired
  };
  constructor(props) {
    super(props);
    this.state = {};
  }

  dotClicked = address => {
    this.props.handleChangeAddress(address, !this.props.addressColors[address].checked);
  };

  dotHover = (address, over) => {
    if (this.props.handleHoverAddress) {
      this.props.handleHoverAddress(address, over);
    }
  };

  coloredDot = (address, i) => {
    const color = this.props.addressColors[address].color;
    const checkColor = this.props.addressColors[address].checked ? "white" : color;
    const darker = d3.rgb(color).darker();
    const bgColor = {
      backgroundColor: color,
      border: `1px solid ${darker}`,
      color: checkColor
    };
    return (
      <span
        className="colored-dot"
        id={`address-dot-${i}`}
        aria-label="colored dot"
        onClick={() => this.dotClicked(address)}
        onMouseOver={() => this.dotHover(address, true)}
        onMouseOut={() => this.dotHover(address, false)}
      >
        <span className="colored-dot-dot" style={bgColor}>
          <FA name="check" />
        </span>
        <span className="colored-dot-text">{address}</span>
      </span>
    );
  };

  render() {
    return (
      <ul className="addresses">
        {Object.keys(this.props.addressColors).length === 0 ? (
          <li key={`address-empty`}>There is no traffic</li>
        ) : (
          Object.keys(this.props.addressColors).map((address, i) => {
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
