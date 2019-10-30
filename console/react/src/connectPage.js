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
      nextProps.location &&
      nextProps.location.state &&
      nextProps.location.state.pathname
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
      <React.Fragment>
        <PageSection
          variant={PageSectionVariants.light}
          className="connect-page"
        >
          {showForm ? (
            <ConnectForm
              prefix="form"
              handleConnect={this.props.handleConnect}
              handleConnectCancel={this.handleConnectCancel}
              fromPath={from.pathname}
              isConnectFormOpen={true}
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
                The console is an HTML based web site that displays information
                about a qpid dispatch router network. The console only provides
                limited information about the clients that are attached to the
                router network and is therfore more appropriate for
                administrators needing to know the layout and health of the
                router network.
              </Text>
            </TextContent>
          </div>
        </PageSection>
      </React.Fragment>
    );
  }
}

export default ConnectPage;
