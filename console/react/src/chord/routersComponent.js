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
import PropTypes from "prop-types";

class RoutersComponent extends Component {
  static propTypes = {
    arcColors: PropTypes.object.isRequired,
    handleHoverRouter: PropTypes.func.isRequired
  };

  constructor(props) {
    super(props);
    this.state = {};
  }

  render() {
    return (
      <React.Fragment>
        <ul className="routers">
          {Object.keys(this.props.arcColors).length === 0 ? (
            <li key={`colors-empty`}>There is no traffic</li>
          ) : (
            Object.keys(this.props.arcColors).map((router, i) => {
              return (
                <li
                  key={`router-${i}`}
                  className="legend-line"
                  onMouseEnter={() => this.props.handleHoverRouter(router, true)}
                  onMouseLeave={() => this.props.handleHoverRouter(router, false)}
                >
                  <span
                    className="legend-color"
                    style={{ backgroundColor: this.props.arcColors[router] }}
                  ></span>
                  <span className="legend-router legend-text" title={router}>
                    {router}
                  </span>
                </li>
              );
            })
          )}
        </ul>
      </React.Fragment>
    );
  }
}

export default RoutersComponent;
