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
import { unstable_HistoryRouter as HistoryRouter } from "react-router-dom";
import { createHashHistory } from "history";

import "@patternfly/patternfly/patternfly.css";
import "@patternfly/patternfly/patternfly-addons.css";
import "./patternfly.css";
import "font-awesome/css/font-awesome.min.css";
import "@patternfly/patternfly/components/Nav/nav.css";
import { QDRService } from "./common/qdrService";
import "./App.css";
import PageLayout from "./overview/dashboard/layout";

function App(props) {

  // service is passed in to make testing easier
  const service = new QDRService();

  // also, a router is used here to provide PageLayout with a history property
  const history = createHashHistory({ window });

  return (
    <HistoryRouter history={history}>
      <div className="App pf-m-redhat-font">
        <PageLayout service={service} {...props} history={history} config={props.config}/>
      </div>
    </HistoryRouter>
  );
}

export default App;
