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
import {ToolbarGroup, ToolbarItem} from "@patternfly/react-core";
import OptionsComponent from "./optionsComponent";
import RoutersComponent from "./routersComponent";
import DropdownPanel from "../common/dropdownPanel";
import PropTypes from "prop-types";

class ChordToolbar extends React.Component {
  static propTypes = {
    isRate: PropTypes.bool.isRequired,
    byAddress: PropTypes.bool.isRequired,
    addresses: PropTypes.object.isRequired,
    chordColors: PropTypes.object.isRequired,
    arcColors: PropTypes.object.isRequired,
    handleChangeAddress: PropTypes.func.isRequired,
    handleChangeOption: PropTypes.func.isRequired,
    handleHoverAddress: PropTypes.func.isRequired,
    handleHoverRouter: PropTypes.func.isRequired
  };

  render() {
    return (
      <ToolbarGroup>
        <ToolbarItem data-testid="chord-toolbar" className="pf-u-mr-md">
          <DropdownPanel
            title="Options"
            panel={
              <OptionsComponent
                isRate={this.props.isRate}
                byAddress={this.props.byAddress}
                handleChangeOption={this.props.handleChangeOption}
                addresses={this.props.addresses}
                addressColors={this.props.chordColors}
                handleChangeAddress={this.props.handleChangeAddress}
                handleHoverAddress={this.props.handleHoverAddress}
              />
            }
          />
        </ToolbarItem>
        <ToolbarItem className="pf-u-mr-md">
          <DropdownPanel
            title="Routers"
            panel={
              <RoutersComponent
                arcColors={this.props.arcColors}
                handleHoverRouter={this.props.handleHoverRouter}
              />
            }
          />
        </ToolbarItem>
      </ToolbarGroup>
    );
  }
}

export default ChordToolbar;
