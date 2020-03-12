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
import ContextMenuComponent from "./contextMenuComponent";
import PropTypes from "prop-types";

class DropdownMenu extends Component {
  static propTypes = {
    isVisible: PropTypes.bool,
    handleDropdownLogout: PropTypes.func.isRequired,
    isConnected: PropTypes.func.isRequired,
    handleContextHide: PropTypes.func.isRequired,
    parentClass: PropTypes.string.isRequired,
    handleSuppress: PropTypes.func.isRequired,
    suppress: PropTypes.any.isRequired
  };
  constructor(props) {
    super(props);
    this.state = {
      isVisible:
        typeof this.props.isVisible !== "undefined" ? this.props.isVisible : false
    };
    this.contextMenuItems = [
      {
        title: "Logout",
        action: this.logout,
        enabled: this.isLoggedIn
      },
      {
        title: "Suppress notifications",
        action: this.suppress,
        enabled: () => true,
        suppressIcon: true
      }
    ];
  }

  logout = (item, data, event) => {
    this.props.handleDropdownLogout();
  };

  suppress = () => {
    this.props.handleSuppress();
  };

  isLoggedIn = () => {
    return this.props.isConnected();
  };

  show = isVisible => {
    this.setState({ isVisible });
  };

  render() {
    return (
      this.state.isVisible && (
        <ContextMenuComponent
          className="layout-dropdown"
          contextEventPosition={[-1, -1]} // show in-place
          handleContextHide={this.props.handleContextHide}
          menuItems={this.contextMenuItems}
          suppress={this.props.suppress}
          parentClass={this.props.parentClass}
        />
      )
    );
  }
}

export default DropdownMenu;
