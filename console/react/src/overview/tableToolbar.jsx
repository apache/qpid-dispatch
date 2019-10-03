import React from "react";
import {
  Dropdown,
  DropdownPosition,
  DropdownToggle,
  DropdownItem,
  Pagination,
  TextInput,
  Toolbar,
  ToolbarGroup,
  ToolbarItem
} from "@patternfly/react-core";

class TableToolbar extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      isDropDownOpen: false,
      searchValue: ""
    };
    this.handleTextInputChange = value => {
      this.setState({ searchValue: value });
    };

    this.onDropDownToggle = isOpen => {
      this.setState({
        isDropDownOpen: isOpen
      });
    };

    this.onDropDownSelect = event => {
      this.setState({
        isDropDownOpen: !this.state.isDropDownOpen
      });
    };

    this.buildSearchBox = () => {
      let { value } = this.state.searchValue;
      return (
        <TextInput
          value={value ? value : ""}
          type="search"
          onChange={this.handleTextInputChange}
          aria-label="search text input"
        />
      );
    };

    this.buildDropdown = () => {
      const { isDropDownOpen } = this.state;
      return (
        <Dropdown
          onSelect={this.onDropDownSelect}
          position={DropdownPosition.right}
          toggle={
            <DropdownToggle onToggle={this.onDropDownToggle}>
              All
            </DropdownToggle>
          }
          isOpen={isDropDownOpen}
          dropdownItems={[
            <DropdownItem key="item-1">Item 1</DropdownItem>,
            <DropdownItem key="item-2">Item 2</DropdownItem>,
            <DropdownItem key="item-3">Item 3</DropdownItem>,
            <DropdownItem isDisabled key="all">
              All
            </DropdownItem>
          ]}
        />
      );
    };
  }

  render() {
    return (
      <Toolbar className="pf-l-toolbar pf-u-mx-xl pf-u-my-md table-toolbar">
        <ToolbarGroup>
          <ToolbarItem className="pf-u-mr-md">
            {this.buildDropdown()}
          </ToolbarItem>
          <ToolbarItem className="pf-u-mr-xl">
            {this.buildSearchBox()}
          </ToolbarItem>
        </ToolbarGroup>
        <ToolbarGroup className="toolbar-pagination">
          <ToolbarItem>
            <Pagination
              itemCount={this.props.total}
              page={this.props.page}
              perPage={this.props.perPage}
              onSetPage={(_evt, value) => this.props.onSetPage(value)}
              onPerPageSelect={(_evt, value) =>
                this.props.onPerPageSelect(value)
              }
              variant={"top"}
            />
          </ToolbarItem>
        </ToolbarGroup>
      </Toolbar>
    );
  }
}

export default TableToolbar;
