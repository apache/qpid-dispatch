import React, { Component } from "react";
import "@patternfly/patternfly/patternfly.css";
import "@patternfly/patternfly/patternfly-addons.css";

import "patternfly/dist/css/patternfly.css";
import "patternfly/dist/css/patternfly-additions.css";
import "@patternfly/patternfly/components/Nav/nav.css";
import { QDRService } from "./qdrService";
import "./App.css";
import PageLayout from "./layout";
class App extends Component {
  state = {};

  render() {
    const service = new QDRService();
    return (
      <div className="App pf-m-redhat-font">
        <PageLayout service={service} config={this.props.config} />
      </div>
    );
  }
}

export default App;
