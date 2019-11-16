import React from "react";
import { Toolbar, ToolbarGroup, ToolbarItem } from "@patternfly/react-core";
import TrafficComponent from "./trafficComponent";
import MapComponent from "./mapComponent";
import ArrowsComponent from "./arrowsComponent";
import DropdownPanel from "../dropdownPanel"


class TopologyToolbar extends React.Component {
  render() {
    return (
      <Toolbar data-testid="topology-toolbar" className="pf-l-toolbar pf-u-justify-content-space-between pf-u-mx-xl pf-u-my-md">
        <ToolbarGroup>
          <ToolbarItem className="pf-u-mr-md">
            <DropdownPanel
              title="Traffic"
              panel={
                <TrafficComponent
                  addresses={this.props.legendOptions.traffic.addresses}
                  addressColors={this.props.legendOptions.traffic.addressColors}
                  handleChangeTrafficAnimation={
                    this.props.handleChangeTrafficAnimation
                  }
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
      </Toolbar>
    );
  }
}

export default TopologyToolbar;
