import React from "react";
import {
  Button,
  ButtonVariant,
  InputGroup,
  TextInput,
  Toolbar,
  ToolbarGroup,
  ToolbarItem
} from "@patternfly/react-core";
import {
  SearchIcon,
  SortAlphaDownIcon,
  SortAlphaUpIcon
} from "@patternfly/react-icons";
import Confirm from "./confirm";

class EdgeTableToolbar extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      isDropDownOpen: false,
      isKebabOpen: false,
      searchValue: ""
    };
    this.handleTextInputChange = searchValue => {
      this.setState({ searchValue });
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

    this.onKebabToggle = isOpen => {
      this.setState({
        isKebabOpen: isOpen
      });
    };

    this.onKebabSelect = event => {
      this.setState({
        isKebabOpen: !this.state.isKebabOpen
      });
    };

    this.buildSearchBox = () => {
      return (
        <InputGroup>
          <TextInput
            value={this.props.filterText}
            type="search"
            onChange={this.props.handleChangeFilter}
            aria-label="search text input"
            placeholder="search for edge namespaces"
          />
          <Button
            variant={ButtonVariant.tertiary}
            aria-label="search button for search input"
          >
            <SearchIcon />
          </Button>
        </InputGroup>
      );
    };
  }

  isDeleteDisabled = () => this.props.rows.every(r => !r.selected);
  deleteText = () => {
    return (
      <React.Fragment>
        <h2>Are you sure you want to delete:</h2>
        <ul>
          {this.props.rows
            .filter(r => r.selected)
            .map((r, i) => {
              return <li key={`key-${i}`}>{r.name}</li>;
            })}
        </ul>
      </React.Fragment>
    );
  };
  render() {
    return (
      <Toolbar className="pf-l-toolbar pf-u-justify-content-space-between pf-u-mx-xl pf-u-my-md">
        <ToolbarGroup>
          <ToolbarItem className="pf-u-mr-md">
            {this.buildSearchBox()}
          </ToolbarItem>
          <ToolbarItem>
            <Button
              variant="plain"
              onClick={this.props.toggleAlphaSort}
              aria-label="Sort A-Z"
            >
              {this.props.sortDown ? (
                <SortAlphaDownIcon />
              ) : (
                <SortAlphaUpIcon />
              )}
            </Button>
          </ToolbarItem>
        </ToolbarGroup>
        <ToolbarGroup className="edge-table-actions">
          <ToolbarItem className="pf-u-mx-sm">
            <Confirm
              handleConfirm={this.props.handleDeleteEdge}
              buttonText="Delete"
              title="Confirm delete"
              isDeleteDisabled={this.isDeleteDisabled()}
            >
              {this.deleteText()}
            </Confirm>
          </ToolbarItem>
          <ToolbarItem className="pf-u-mx-sm">
            <Button aria-label="Add" onClick={this.props.handleAddEdge}>
              Add
            </Button>
          </ToolbarItem>
        </ToolbarGroup>
      </Toolbar>
    );
  }
}

export default EdgeTableToolbar;
