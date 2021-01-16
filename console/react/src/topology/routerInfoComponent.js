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
import { Modal, ModalVariant } from "@patternfly/react-core";

class RouterInfoComponent extends Component {
  constructor(props) {
    super(props);
    this.state = {
      toolTip: null
    };
    this.timer = null;
  }

  componentDidMount = () => {
    this.timer = setInterval(this.getTooltip, 5000);
    this.getTooltip();
  };

  componentWillUnmount = () => {
    this.unmounted = true;
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  };

  getTooltip = () => {
    this.props.d.toolTip(this.props.topology, true).then(toolTip => {
      if (this.unmounted) return;
      this.setState({ toolTip });
    });
  };

  render() {
    return (
      <Modal
        variant={ModalVariant.small}
        title="Router info"
        isOpen={true}
        onClose={this.props.handleCloseRouterInfo}
      >
        {this.state.toolTip ? (
          <div
            className="popup-info"
            dangerouslySetInnerHTML={{ __html: this.state.toolTip }}
          />
        ) : (
          <div>Loading...</div>
        )}
      </Modal>
    );
  }
}

export default RouterInfoComponent;
