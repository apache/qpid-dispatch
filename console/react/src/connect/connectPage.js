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
  PageSection,
  PageSectionVariants,
  TextContent,
  Text
} from "@patternfly/react-core";
import ConnectForm from "./connect-form";

class ConnectPage extends React.Component {
  constructor(props) {
    super(props);
    this.state = { showForm: true };
  }

  handleConnectCancel = () => {
    this.setState({ showForm: false });
  };

  shouldComponentUpdate = (nextProps, nextState) => {
    if (nextState.showForm !== this.state.showForm) return true;
    const nextPathname =
      nextProps.location && nextProps.location.state && nextProps.location.state.pathname
        ? nextProps.location.state.pathname
        : undefined;
    const currentPathname =
      this.props.location &&
      this.props.location.state &&
      this.props.location.state.pathname
        ? this.props.location.state.pathname
        : undefined;

    return nextPathname !== currentPathname;
  };

  render() {
    const { showForm } = this.state;
    const { from } = this.props.location.state || { from: { pathname: "/" } };
    return (
      <PageSection variant={PageSectionVariants.light} className="connect-page">
        {showForm ? (
          <ConnectForm
            prefix="form"
            service={this.props.service}
            handleConnect={this.props.handleConnect}
            handleConnectCancel={this.handleConnectCancel}
            handleAddNotification={this.props.handleAddNotification}
            fromPath={from.pathname}
            isConnectFormOpen={true}
            connecting={this.props.connecting}
            connectingTitle={this.props.connectingTitle}
            connectingMessage={this.props.connectingMessage}
            isConnected={false}
          />
        ) : (
          <React.Fragment />
        )}
        <div className="left-content">
          <TextContent>
            <Text component="h1" className="console-banner">
              {this.props.config.title}
            </Text>
          </TextContent>
          <TextContent>
            <Text component="h2" className="connect-description">
              The console displays information about a Qpid Dispatch Router network. It
              allows monitoring and management control of the router's entities.
            </Text>
            <Text component="h2" className="connect-description">
              The console only provides limited information about the clients that are
              attached to the router network and is therfore more appropriate for
              administrators needing to know the layout and health of the router network.
            </Text>
          </TextContent>
        </div>
      </PageSection>
    );
  }
}

export default ConnectPage;
