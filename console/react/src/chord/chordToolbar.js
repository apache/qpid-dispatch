import React from "react";
import { Toolbar, ToolbarGroup, ToolbarItem } from "@patternfly/react-core";
import OptionsComponent from "./optionsComponent";
import RoutersComponent from "./routersComponent";
import DropdownPanel from "../dropdownPanel"

class ChordToolbar extends React.Component {

  render() {
    return (
      <Toolbar
        data-testid="chord-toolbar"
        className="pf-l-toolbar pf-u-justify-content-space-between pf-u-mx-xl pf-u-my-md">
        <ToolbarGroup>
          <ToolbarItem className="pf-u-mr-md">
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
      </Toolbar>
    );
  }
}

export default ChordToolbar;
