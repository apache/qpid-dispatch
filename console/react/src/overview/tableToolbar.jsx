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
      searchValue: "",
      filterBy: this.props.fields[0].title
    };
    this.handleTextInputChange = value => {
      this.setState({ searchValue: value }, () => {
        this.props.handleChangeFilterValue(
          this.state.filterBy,
          this.state.searchValue
        );
      });
    };

    this.onDropDownToggle = isDropDownOpen => {
      this.setState({
        isDropDownOpen
      });
    };

    this.onDropDownSelect = event => {
      this.setState(
        {
          isDropDownOpen: !this.state.isDropDownOpen,
          filterBy: event.target.text,
          searchValue: ""
        },
        () =>
          this.props.handleChangeFilterValue(
            this.state.filterBy,
            this.state.searchValue
          )
      );
    };

    this.buildSearchBox = () => {
      return (
        <TextInput
          value={this.state.searchValue}
          type="search"
          onChange={this.handleTextInputChange}
          aria-label="search text input"
          placeholder="Filter by..."
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
              {this.state.filterBy}
            </DropdownToggle>
          }
          isOpen={isDropDownOpen}
          dropdownItems={this.props.fields.map(f => {
            return (
              <DropdownItem key={`item-${f.title}`}>{f.title}</DropdownItem>
            );
          })}
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
