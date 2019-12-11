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
import { Legend } from "./legend.js";

class LegendComponent extends Component {
  constructor(props) {
    super(props);
    this.state = {};
    this.legend = new Legend(this.props.nodes);
  }

  componentDidMount = () => {
    this.legend.update();
  };

  componentDidUpdate = () => {
    console.log("legend did update");
  };

  render() {
    return (
      <div
        id="topologyLegend"
        className="pf-c-modal-box pf-u-box-shadow-md pf-m-sm"
        role="dialog"
        aria-modal="true"
        aria-labelledby="modal-lg-title"
        aria-describedby="modal-lg-description"
      >
        <header>
          <h1 className="pf-c-title pf-m-xl" id="modal-lg-title">
            Topology Legend
          </h1>
          <button
            className="pf-c-button pf-m-plain"
            type="button"
            aria-label="Close"
            onClick={this.props.handleCloseLegend}
          >
            <i className="fas fa-times" aria-hidden="true"></i>
          </button>
        </header>
        <div className="pf-c-modal-box__body" id="modal-lg-description">
          <div id="topo_svg_legend"></div>{" "}
        </div>
      </div>
    );
  }
}

export default LegendComponent;
