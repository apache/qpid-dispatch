import React from "react";
import { Toolbar, ToolbarGroup, ToolbarItem } from "@patternfly/react-core";
import { Popover, PopoverPosition, Button } from "@patternfly/react-core";
import OptionsComponent from "./optionsComponent";
import RoutersComponent from "./routersComponent";

class ChordToolbar extends React.Component {
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
              aria-label="Options"
              closeBtnAriaLabel="Close Popover options"
              bodyContent={
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
            >
              <Button className="topology-toolbar-button" value="options">
                <span className="pf-c-dropdown__toggle-text">Options</span>
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
              aria-label="Popover routers"
              closeBtnAriaLabel="Close Popover routers"
              bodyContent={
                <RoutersComponent
                  arcColors={this.props.arcColors}
                  handleHoverRouter={this.props.handleHoverRouter}
                />
              }
            >
              <Button className="topology-toolbar-button" value="routers">
                <span className="pf-c-dropdown__toggle-text">Routers</span>
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

export default ChordToolbar;
