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
import {
  Button,
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
    const actions = this.props.actions.map(action => (
      <ToolbarItem className="pf-u-mx-md">
        <Button aria-label={action}>{action}</Button>
      </ToolbarItem>
    ));

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
        {this.props.actions && <ToolbarGroup>{actions}</ToolbarGroup>}
        {!this.props.hidePagination && (
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
        )}
      </Toolbar>
    );
  }
}

export default TableToolbar;
