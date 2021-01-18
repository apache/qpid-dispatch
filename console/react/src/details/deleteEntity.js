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

class DeleteEntity extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      isModalOpen: false,
      closing: false,
      closed: false
    };
  }

  handleModalShow = () => {
    this.setState({ isModalOpen: true, closed: false });
  };

  handleModalHide = () => {
    this.setState({ isModalOpen: false, closed: true }, () => {
      if (this.props.cancelledAction) {
        this.props.cancelledAction("DELETE");
      }
    });
  };

  getName = record => {
    return record.name !== null ? record.name : `${this.props.entity}/${record.identity}`;
  };

  delete = () => {
    this.setState({ closing: true }, () => {
      const record = this.props.record;
      const name = this.getName(record);
      this.props.service.management.connection
        .sendMethod(
          record.nodeId || record.routerId,
          this.props.entity,
          { identity: record.identity },
          "DELETE"
        )
        .then(results => {
          let statusCode = results.context.message.application_properties.statusCode;
          if (statusCode < 200 || statusCode >= 300) {
            const msg = `Deleted ${name} failed with message: ${results.context.message.application_properties.statusDescription}`;
            console.log(`error ${msg}`);
            this.props.handleAddNotification("action", msg, new Date(), "danger");
          } else {
            const msg = `Deleted ${this.props.entity} ${name}`;
            console.log(`success ${msg}`);
            this.props.handleAddNotification("action", msg, new Date(), "success");
          }
          this.setState({ isModalOpen: false, closing: false, closed: true }, () => {
            if (this.props.notifyClick) {
              this.props.notifyClick("Done");
            }
          });
        });
    });
  };

  render() {
    const { isModalOpen, closing } = this.state;
    const record = this.props.record;
    const name = this.getName(record);
    return (
      <React.Fragment>
        {!this.props.showNow && (
          <Button
            aria-label="delete-entity-button"
            className={`${this.props.asButton ? "" : "link-button"}`}
            onClick={this.handleModalShow}
          >
            Delete
          </Button>
        )}
        <Modal
          variant={ModalVariant.small}
          title={`Delete this ${this.props.entity}?`}
          isOpen={isModalOpen || (this.props.showNow && !this.state.closed)}
          onClose={this.handleModalHide}
          actions={[
            <Button
              aria-label="confirm-delete"
              key="confirm"
              variant="primary"
              onClick={this.delete}
              isDisabled={closing}
            >
              Delete
            </Button>,
            <Button
              aria-label="cancel-delete"
              key="cancel"
              variant="link"
              onClick={this.handleModalHide}
              isDisabled={closing}
            >
              Cancel
            </Button>
          ]}
        >
          {closing ? `Deleting ${name}` : `${name}`}
        </Modal>
      </React.Fragment>
    );
  }
}

export default DeleteEntity;
