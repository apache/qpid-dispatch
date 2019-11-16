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

import React, { Component } from "react";
import { Checkbox } from "@patternfly/react-core";

class ArrowsComponent extends Component {
  constructor(props) {
    super(props);
    this.state = {};
  }

  render() {
    return (
      <div id="arrows-expand">
        <Checkbox
          label="Show arrows between routers"
          isChecked={this.props.routerArrows}
          onChange={this.props.handleChangeArrows}
          aria-label="show router arrows"
          id="check-router-arrows"
          name="routerArrows"
        />
        <Checkbox
          label="Show arrows to clients"
          isChecked={this.props.clientArrows}
          onChange={this.props.handleChangeArrows}
          aria-label="show client arrows"
          id="check-client-arrows"
          name="clientArrows"
        />
      </div>
    );
  }
}

export default ArrowsComponent;
