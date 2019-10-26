import React from "react";
import { Toolbar, ToolbarGroup, ToolbarItem } from "@patternfly/react-core";
import { Popover, PopoverPosition, Button } from "@patternfly/react-core";
import TrafficComponent from "./trafficComponent";
import MapComponent from "./mapComponent";
import ArrowsComponent from "./arrowsComponent";

class TopologyToolbar extends React.Component {
  constructor(props) {
    super(props);

    this.state = {
      isOpen: { traffic: false, arrows: false, map: false }
    };
  }
  handleShowPopover = event => {
    console.log("handleShowPopover called");
    const { isOpen } = this.state;
    const value = event.target.value;
    isOpen[value] = !isOpen[value];
    this.setState({ isOpen });
  };

  shouldClose = tip => {
    console.log("should close called");
    const { isOpen } = this.state;
    const value = tip.reference.value;
    isOpen[value] = false;
    this.setState({ isOpen });
  };

  render() {
    return (
      <Toolbar className="pf-l-toolbar pf-u-justify-content-space-between pf-u-mx-xl pf-u-my-md">
        <ToolbarGroup>
          <ToolbarItem className="pf-u-mr-md">
            <Popover
              className="topology-toolbar-item"
              position={PopoverPosition.bottom}
              enableFlip={false}
              appendTo={() => document.getElementById("root")}
              aria-label="Popover traffic"
              closeBtnAriaLabel="Close Popover traffic"
              bodyContent={
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
            >
              <Button className="topology-toolbar-button" value="traffic">
                <span className="pf-c-dropdown__toggle-text">Traffic</span>
                <i
                  className="fas fa-caret-down pf-c-dropdown__toggle-icon"
                  aria-hidden="true"
                ></i>
              </Button>
            </Popover>
          </ToolbarItem>
          <ToolbarItem className="pf-u-mr-md">
            <Popover
              className="topology-toolbar-item"
              position={PopoverPosition.bottom}
              enableFlip={false}
              appendTo={() => document.getElementById("root")}
              aria-label="Popover map"
              closeBtnAriaLabel="Close Popover map"
              bodyContent={
                <MapComponent
                  mapShown={this.props.legendOptions.map.show}
                  areaColor={this.props.mapOptions.areaColor}
                  oceanColor={this.props.mapOptions.oceanColor}
                  handleUpdateMapColor={this.props.handleUpdateMapColor}
                  handleUpdateMapShown={this.props.handleUpdateMapShown}
                />
              }
            >
              <Button className="topology-toolbar-button" value="map">
                <span className="pf-c-dropdown__toggle-text">
                  Background map
                </span>
                <i
                  className="fas fa-caret-down pf-c-dropdown__toggle-icon"
                  aria-hidden="true"
                ></i>
              </Button>
            </Popover>
          </ToolbarItem>
          <ToolbarItem className="pf-u-mr-md">
            <Popover
              className="topology-toolbar-item"
              position={PopoverPosition.bottom}
              enableFlip={false}
              appendTo={() => document.getElementById("root")}
              aria-label="Popover arrows"
              closeBtnAriaLabel="Close Popover arrows"
              bodyContent={
                <ArrowsComponent
                  routerArrows={this.props.legendOptions.arrows.routerArrows}
                  clientArrows={this.props.legendOptions.arrows.clientArrows}
                  handleChangeArrows={this.props.handleChangeArrows}
                />
              }
            >
              <Button className="topology-toolbar-button" value="arrows">
                <span className="pf-c-dropdown__toggle-text">Arrows</span>
                <i
                  className="fas fa-caret-down pf-c-dropdown__toggle-icon"
                  aria-hidden="true"
                ></i>
              </Button>
            </Popover>
          </ToolbarItem>
        </ToolbarGroup>
      </Toolbar>
    );
  }
}

export default TopologyToolbar;
