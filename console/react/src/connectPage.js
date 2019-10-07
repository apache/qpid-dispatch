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
  render() {
    const { showForm } = this.state;
    const { from } = this.props.location.state || { from: { pathname: "/" } };
    return (
      <PageSection variant={PageSectionVariants.light} className="connect-page">
        {showForm ? (
          <ConnectForm
            prefix="form"
            handleConnect={this.props.handleConnect}
            handleConnectCancel={this.handleConnectCancel}
            fromPath={from.pathname}
          />
        ) : (
          <React.Fragment />
        )}
        <div className="left-content">
          <TextContent>
            <Text component="h1" className="console-banner">
              Apache Qpid Dispatch Console
            </Text>
          </TextContent>
          <TextContent>
            <Text component="p">
              The console provides limited information about the clients that
              are attached to the router network and is therefore more
              appropriate for administrators needing to know the layout and
              health of the router network.
            </Text>
          </TextContent>
        </div>
      </PageSection>
    );
  }
}

export default ConnectPage;
