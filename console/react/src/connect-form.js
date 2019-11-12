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
import PleaseWait from "./pleaseWait";
const CONNECT_KEY = "QDRSettings";

class ConnectForm extends React.Component {
  constructor(props) {
    super(props);

    this.state = {
      address: "",
      port: "",
      username: "",
      password: "",
      isShown: this.props.isConnectFormOpen,
      connecting: false,
      connectError: null
    };
  }

  show = isShown => {
    this.setState({ isShown });
  };

  componentDidMount = () => {
    let savedValues = localStorage.getItem(CONNECT_KEY);
    if (!savedValues) {
      savedValues = {
        address: "localhost",
        port: "5673",
        username: "",
        password: ""
      };
    } else {
      savedValues = JSON.parse(savedValues);
    }
    this.setState(savedValues);
  };
  handleTextInputChange = (field, value) => {
    const formValues = Object.assign(this.state);
    formValues[field] = value;
    this.setState(formValues, () => {
      const state2Save = JSON.parse(JSON.stringify(formValues));
      // don't save the password
      state2Save.password = "";
      localStorage.setItem(CONNECT_KEY, JSON.stringify(state2Save));
    });
  };

  handleConnect = () => {
    if (this.props.isConnected) {
      // handle disconnects from the main page
      this.props.handleConnect(this.props.fromPath);
    } else {
      const connectOptions = JSON.parse(JSON.stringify(this.state));
      if (connectOptions.username === "") connectOptions.username = undefined;
      if (connectOptions.password === "") connectOptions.password = undefined;
      connectOptions.reconnect = true;

      this.setState({ connecting: true }, () => {
        this.props.service.connect(connectOptions).then(
          r => {
            this.setState({ connecting: false });
            this.props.handleConnect(this.props.fromPath, r);
          },
          e => {
            console.log(e);
            this.setState({ connecting: false, connectError: e.msg });
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
      isShown,
      address,
      port,
      username,
      password,
      connecting
    } = this.state;

    return isShown ? (
      <div>
        <div className="connect-modal">
          <div className={connecting ? "connecting" : ""}>
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
                  value={address}
                  isRequired
                  type="text"
                  id={`form-address-${this.props.prefix}`}
                  aria-describedby="horizontal-form-address-helper"
                  name="form-address"
                  onChange={value =>
                    this.handleTextInputChange("address", value)
                  }
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
              <FormGroup
                label="User name"
                fieldId={`form-user-${this.props.prefix}`}
              >
                <TextInput
                  value={username}
                  onChange={value =>
                    this.handleTextInputChange("username", value)
                  }
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
                  value={password}
                  onChange={value =>
                    this.handleTextInputChange("password", value)
                  }
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
          <PleaseWait
            isOpen={connecting}
            title="Connecting"
            message="Connecting to the router, please wait..."
          />
        </div>
      </div>
    ) : null;
  }
}

export default ConnectForm;
