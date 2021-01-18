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
import { Button, Modal, ModalVariant } from "@patternfly/react-core";
import PropTypes from "prop-types";

class ConnectionClose extends React.Component {
  static propTypes = {
    extraInfo: PropTypes.object.isRequired,
    service: PropTypes.object.isRequired,
    handleAddNotification: PropTypes.func.isRequired,
    asButton: PropTypes.bool,
    notifyClick: PropTypes.func
  };
  constructor(props) {
    super(props);
    this.state = {
      isModalOpen: false,
      closing: false
    };
  }

  handleModalToggle = () => {
    this.setState(({ isModalOpen }) => ({
      isModalOpen: !isModalOpen
    }));
  };

  closeConnection = () => {
    this.setState({ closing: true }, () => {
      const record = this.props.extraInfo.rowData.data;
      this.props.service.management.connection
        .sendMethod(
          record.nodeId || record.routerId,
          "connection",
          { adminStatus: "deleted", identity: record.identity },
          "UPDATE",
          { adminStatus: "deleted" }
        )
        .then(results => {
          let statusCode = results.context.message.application_properties.statusCode;
          if (statusCode < 200 || statusCode >= 300) {
            console.log(
              `error ${record.name} ${results.context.message.application_properties.statusDescription}`
            );
            this.props.handleAddNotification(
              "action",
              `Close connection ${record.name} failed with message: ${results.context.message.application_properties.statusDescription}`,
              new Date(),
              "danger"
            );
          } else {
            this.props.handleAddNotification(
              "action",
              `Closed connection ${record.name}`,
              new Date(),
              "success"
            );
            console.log(
              `success ${record.name} ${results.context.message.application_properties.statusDescription}`
            );
          }
          this.setState({ isModalOpen: false, closing: false }, () => {
            if (this.props.notifyClick) {
              this.props.notifyClick();
            }
          });
        });
    });
  };

  render() {
    const { isModalOpen, closing } = this.state;
    const record = this.props.extraInfo.rowData.data;
    if (record.role === "normal") {
      return (
        <React.Fragment>
          <Button
            className={`${this.props.asButton ? "" : "link-button"}`}
            onClick={this.handleModalToggle}
            aria-label="connection-close-button"
          >
            Close
          </Button>
          <Modal
            variant={ModalVariant.small}
            aria-label="connection-close-modal"
            title="Close connection"
            isOpen={isModalOpen}
            onClose={this.handleModalToggle}
            actions={[
              <Button
                aria-label="connection-close-confirm"
                key="confirm"
                variant="primary"
                onClick={this.closeConnection}
                isDisabled={closing}
              >
                Confirm
              </Button>,
              <Button
                key="cancel"
                variant="link"
                onClick={this.handleModalToggle}
                isDisabled={closing}
              >
                Cancel
              </Button>
            ]}
          >
            {closing
              ? `Closing connection ${record.name}`
              : `Close connection ${record.name}?`}
          </Modal>
        </React.Fragment>
      );
    } else {
      return <React.Fragment />;
    }
  }
}

export default ConnectionClose;
