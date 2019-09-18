import React from "react";
import {
  FormGroup,
  Text,
  TextContent,
  TextInput,
  Split,
  SplitItem
} from "@patternfly/react-core";

class NetworkName extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      editing: true,
      editingName: this.props.networkInfo.name
    };
  }

  handleSaveClick = () => {
    let editing = !this.state.editing;
    this.setState({ editing });
    if (!editing) this.props.handleNetworkNameChange(this.state.editingName);
  };

  handleCancelClick = () => {
    let { editing, editingName } = this.state;
    editing = false;
    editingName = this.props.networkInfo.name;
    this.setState({ editing, editingName });
  };

  handleLocalEditName = editingName => {
    this.setState({ editingName });
  };

  render() {
    return (
      <React.Fragment>
        <Split gutter="md" className="network-name">
          <SplitItem>
            <TextContent className="enter-prompt">
              <Text component="h3">Enter a network name to get started</Text>
            </TextContent>
          </SplitItem>
          <SplitItem isFilled>
            <FormGroup label="" isRequired fieldId="simple-form-name">
              <TextInput
                className={this.state.editing ? "editing" : "not-editing"}
                isRequired
                isDisabled={!this.state.editing}
                type="text"
                id="network-name"
                name="network-name"
                aria-describedby="network-name-helper"
                value={this.state.editingName}
                onChange={this.handleLocalEditName}
              />
            </FormGroup>
          </SplitItem>
        </Split>
      </React.Fragment>
    );
  }
}

export default NetworkName;
