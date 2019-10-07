import React, { Component } from "react";
import "@patternfly/patternfly/patternfly.css";
import "@patternfly/patternfly/patternfly-addons.css";

import "patternfly/dist/css/patternfly.css";
import "@patternfly/patternfly/components/Nav/nav.css";
import "./App.css";
import PageLayout from "./layout";

class App extends Component {
  state = {};

  render() {
    return (
      <div className="App">
        <PageLayout />
      </div>
    );
  }
}

export default App;
