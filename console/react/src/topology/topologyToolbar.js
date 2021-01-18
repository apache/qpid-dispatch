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
import TrafficComponent from "./trafficComponent";
import MapComponent from "./mapComponent";
import ArrowsComponent from "./arrowsComponent";
import DropdownPanel from "../common/dropdownPanel";

class TopologyToolbar extends React.Component {
  render() {
    return (
      <ToolbarGroup>
        <ToolbarItem data-testid="topology-toolbar" className="pf-u-mr-md">
          <DropdownPanel
            title="Traffic"
            panel={
              <TrafficComponent
                addressColors={this.props.addressColors}
                handleChangeTrafficAnimation={this.props.handleChangeTrafficAnimation}
                handleChangeTrafficFlowAddress={
                  this.props.handleChangeTrafficFlowAddress
                }
                handleHoverAddress={this.props.handleHoverAddress}
                dots={this.props.legendOptions.traffic.dots}
                congestion={this.props.legendOptions.traffic.congestion}
              />
            }
          />
        </ToolbarItem>
        <ToolbarItem className="pf-u-mr-md">
          <DropdownPanel
            title="Background map"
            panel={
              <MapComponent
                mapShown={this.props.legendOptions.map.show}
                areaColor={this.props.mapOptions.areaColor}
                oceanColor={this.props.mapOptions.oceanColor}
                handleUpdateMapColor={this.props.handleUpdateMapColor}
                handleUpdateMapShown={this.props.handleUpdateMapShown}
              />
            }
          />
        </ToolbarItem>
        <ToolbarItem className="pf-u-mr-md">
          <DropdownPanel
            title="Arrows"
            panel={
              <ArrowsComponent
                routerArrows={this.props.legendOptions.arrows.routerArrows}
                clientArrows={this.props.legendOptions.arrows.clientArrows}
                handleChangeArrows={this.props.handleChangeArrows}
              />
            }
          />
        </ToolbarItem>
      </ToolbarGroup>
    );
  }
}

export default TopologyToolbar;
