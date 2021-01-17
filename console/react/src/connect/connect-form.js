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
  Form,
  FormGroup,
  TextInput,
  ActionGroup,
  Button,
  TextContent,
  Text,
  TextVariants
} from "@patternfly/react-core";
import PleaseWait from "../common/pleaseWait";
const CONNECT_KEY = "QDRSettings";

class ConnectForm extends React.Component {
  constructor(props) {
    super(props);

    this.state = {
      address: "",
      port: "",
      username: "",
      password: "",
      connecting: false,
      connectError: null,
      isValid: false
    };
  }

  componentDidMount = () => {
    let savedValues = localStorage.getItem(CONNECT_KEY);
    if (!savedValues) {
      const defaultPort = window.location.protocol.startsWith("https") ? "443" : "80";
      savedValues = {
        address: window.location.hostname,
        port: window.location.port === "" ? defaultPort : window.location.port,
        username: "",
        password: "",
        connectError: null
      };
    } else {
      savedValues = JSON.parse(savedValues);
      savedValues.connectError = null;
    }
    this.setState(savedValues, () => {
      this.setState({ isValid: this.validate() });
    });
    document.addEventListener("keypress", this.handleKeyPress);
    this.validate();
  };

  componentWillUnmount() {
    document.removeEventListener("keypress", this.handleKeyPress);
  }

  handleKeyPress = event => {
    if (this.validate()) {
      if (event.code === "Enter") {
        this.handleConnect();
      }
    }
  };

  validate = () => this.state.address !== "" && this.state.port !== "";

  handleTextInputChange = (field, value) => {
    const formValues = Object.assign(this.state);
    formValues[field] = value;
    this.setState(formValues, () => {
      this.setState({ isValid: this.validate() }, () => {
        const state2Save = JSON.parse(JSON.stringify(formValues));
        // don't save the password
        state2Save.password = "";
        state2Save.connectError = null;
        localStorage.setItem(CONNECT_KEY, JSON.stringify(state2Save));
      });
    });
  };

  handleConnect = () => {
    if (this.props.isConnected) {
      // handle disconnects from the main page
      this.setState({ connecting: false }, () => {
        this.props.handleConnect(this.props.fromPath);
      });
    } else {
      const connectOptions = JSON.parse(JSON.stringify(this.state));
      if (connectOptions.username === "") connectOptions.username = undefined;
      if (connectOptions.password === "") connectOptions.password = undefined;

      this.setState({ connecting: true }, () => {
        connectOptions.reconnect = true;
        this.props.service.connect(connectOptions).then(
          r => {
            this.setState({ connecting: false });
            this.props.handleConnect(this.props.fromPath, r);
          },
          e => {
            console.log(e);
            //this.props.service.setReconnect(false);
            this.setState({ connecting: false, connectError: e.message });
            this.props.handleAddNotification(
              "action",
              `Connect failed with message: ${e.message}`,
              new Date(),
              "danger"
            );
          }
        );
      });
    }
  };

  toggleDrawerHide = () => {
    this.props.handleConnectCancel();
  };

  render() {
    const {
      address,
      port,
      username,
      password,
      connecting,
      connectError,
      isValid
    } = this.state;
    return this.props.isConnectFormOpen ? (
      <div>
        <div className="connect-modal">
          <div className={this.props.connecting || connecting ? "connecting" : ""}>
            <Form isHorizontal>
              <TextContent className="connect-title">
                <Text component={TextVariants.h1}>Connect</Text>
                <Text component={TextVariants.p}>
                  Enter the address and an HTTP-enabled port of a Qpid Dispatch Router.
                </Text>
              </TextContent>
              <FormGroup
                label="Address"
                isRequired
                fieldId={`form-address-${this.props.prefix}`}
              >
                <TextInput
                  value={address}
                  isRequired
                  type="text"
                  id={`form-address-${this.props.prefix}`}
                  aria-describedby="horizontal-form-address-helper"
                  name="form-address"
                  onChange={value => this.handleTextInputChange("address", value)}
                />
              </FormGroup>
              <FormGroup
                label="Port"
                isRequired
                fieldId={`form-port-${this.props.prefix}`}
              >
                <TextInput
                  value={port}
                  onChange={value => this.handleTextInputChange("port", value)}
                  isRequired
                  type="number"
                  id={`form-port-${this.props.prefix}`}
                  name="form-port"
                />
              </FormGroup>
              <FormGroup label="User name" fieldId={`form-user-${this.props.prefix}`}>
                <TextInput
                  value={username}
                  onChange={value => this.handleTextInputChange("username", value)}
                  isRequired
                  id={`form-user-${this.props.prefix}`}
                  name="form-user"
                />
              </FormGroup>
              <FormGroup label="Password" fieldId={`form-password-${this.props.prefix}`}>
                <TextInput
                  value={password}
                  onChange={value => this.handleTextInputChange("password", value)}
                  type="password"
                  id={`form-password-${this.props.prefix}`}
                  name="form-password"
                />
              </FormGroup>
              {connectError && (
                <TextContent className="connect-error">
                  <Text component={TextVariants.p}>{connectError}</Text>
                </TextContent>
              )}
              <ActionGroup>
                <Button
                  variant={this.props.isConnected || isValid ? "primary" : "danger"}
                  isDisabled={this.props.isConnected ? false : !isValid}
                  data-testid="connect-button"
                  onClick={this.handleConnect}
                >
                  {this.props.isConnected ? "Disconnect" : "Connect"}
                </Button>
                <Button variant="secondary" onClick={this.toggleDrawerHide}>
                  Cancel
                </Button>
                <input type="submit" style={{ display: "none" }} />
              </ActionGroup>
            </Form>
          </div>
          <PleaseWait
            isOpen={this.props.connecting || connecting}
            title={this.props.connectingTitle || "Connecting"}
            message={
              this.props.connectingMessage || "Connecting to the router, please wait..."
            }
          />
        </div>
      </div>
    ) : null;
  }
}

export default ConnectForm;
