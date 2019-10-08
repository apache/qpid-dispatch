/*
 * Copyright 2019 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
import React from "react";
import {
  Form,
  FormGroup,
  TextInput,
  ActionGroup,
  Button,
  TextContent,
  Text,
  TextVariants
} from "@patternfly/react-core";

class ConnectForm extends React.Component {
  constructor(props) {
    super(props);

    this.state = {
      value: "please choose",
      value1: "",
      value2: "",
      value3: "",
      value4: ""
    };
    this.handleTextInputChange1 = value1 => {
      this.setState({ value1 });
    };
    this.handleTextInputChange2 = value2 => {
      this.setState({ value2 });
    };
    this.handleTextInputChange3 = value3 => {
      this.setState({ value3 });
    };
    this.handleTextInputChange4 = value4 => {
      this.setState({ value4 });
    };
  }

  handleConnect = () => {
    this.toggleDrawerHide();
    this.props.handleConnect(this.props.fromPath);
  };

  toggleDrawerHide = () => {
    this.props.handleConnectCancel();
  };

  render() {
    const { value1, value2, value3, value4 } = this.state;

    return (
      <div>
        <div className="connect-modal">
          <div className="">
            <Form isHorizontal>
              <TextContent className="connect-title">
                <Text component={TextVariants.h1}>Connect</Text>
                <Text component={TextVariants.p}>
                  Enter the address and an HTTP-enabled port of a qpid dispatch
                  router.
                </Text>
              </TextContent>
              <FormGroup
                label="Address"
                isRequired
                fieldId={`form-address-${this.props.prefix}`}
              >
                <TextInput
                  value={value1}
                  isRequired
                  type="text"
                  id={`form-address-${this.props.prefix}`}
                  aria-describedby="horizontal-form-address-helper"
                  name="form-address"
                  onChange={this.handleTextInputChange1}
                />
              </FormGroup>
              <FormGroup
                label="Port"
                isRequired
                fieldId={`form-port-${this.props.prefix}`}
              >
                <TextInput
                  value={value2}
                  onChange={this.handleTextInputChange2}
                  isRequired
                  type="number"
                  id={`form-port-${this.props.prefix}`}
                  name="form-port"
                />
              </FormGroup>
              <FormGroup
                label="User name"
                fieldId={`form-user-${this.props.prefix}`}
              >
                <TextInput
                  value={value3}
                  onChange={this.handleTextInputChange3}
                  isRequired
                  id={`form-user-${this.props.prefix}`}
                  name="form-user"
                />
              </FormGroup>
              <FormGroup
                label="Password"
                fieldId={`form-password-${this.props.prefix}`}
              >
                <TextInput
                  value={value4}
                  onChange={this.handleTextInputChange4}
                  type="password"
                  id={`form-password-${this.props.prefix}`}
                  name="form-password"
                />
              </FormGroup>
              <ActionGroup>
                <Button variant="primary" onClick={this.handleConnect}>
                  {this.props.isConnected ? "Disconnect" : "Connect"}
                </Button>
                <Button variant="secondary" onClick={this.toggleDrawerHide}>
                  Cancel
                </Button>
              </ActionGroup>
            </Form>
          </div>
        </div>
      </div>
    );
  }
}

export default ConnectForm;
