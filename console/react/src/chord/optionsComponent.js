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

class OptionsComponent extends Component {
  static propTypes = {
    isRate: PropTypes.bool.isRequired,
    handleChangeOption: PropTypes.func.isRequired,
    byAddress: PropTypes.bool.isRequired,
    addresses: PropTypes.object.isRequired,
    addressColors: PropTypes.object.isRequired,
    handleChangeAddress: PropTypes.func.isRequired,
    handleHoverAddress: PropTypes.func.isRequired
  };

  constructor(props) {
    super(props);
    this.state = {};
  }

  render() {
    const addressInfo = {};
    for (const address in this.props.addressColors) {
      addressInfo[address] = {
        color: this.props.addressColors[address],
        checked: this.props.addresses[address]
      };
    }
    return (
      <React.Fragment>
        <Checkbox
          label="Show rates"
          isChecked={this.props.isRate}
          onChange={this.props.handleChangeOption}
          aria-label="show rates"
          id="check-rates"
          name="isRate"
        />
        <Checkbox
          label="Show by address"
          isChecked={this.props.byAddress}
          onChange={this.props.handleChangeOption}
          aria-label="show by address"
          id="check-address"
          name="byAddress"
        />
        {this.props.byAddress && (
          <div className="chord-addresses">
            <AddressesComponent
              addressColors={addressInfo}
              handleChangeAddress={this.props.handleChangeAddress}
              handleHoverAddress={this.props.handleHoverAddress}
            />
          </div>
        )}
      </React.Fragment>
    );
  }
}

export default OptionsComponent;
