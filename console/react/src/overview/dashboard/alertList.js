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
import { Alert, AlertActionCloseButton } from "@patternfly/react-core";

class AlertList extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      alerts: []
    };
    this.nextIndex = 0;
  }

  hideAlert = alert => {
    alert.hiding = true;
    alert.adding = false;
    this.setState({ alerts: this.state.alerts });
    const self = this;
    alert.timer = setTimeout(() => self.alertRemoved(alert), 1000);
  };

  addAlert = (type, message) => {
    const { alerts } = this.state;
    const alert = { key: this.nextIndex++, type, message, adding: true };
    const self = this;
    alert.timer = setTimeout(() => self.hideAlert(alert), 4000);
    alerts.unshift(alert);
    this.setState({ alerts });
  };

  alertRemoved = alert => {
    const { alerts } = this.state;
    const index = alerts.findIndex(a => a.key === alert.key);
    if (index >= 0) alerts.splice(index, 1);
    this.setState({ alerts });
  };

  handleMouseOver = alert => {
    clearTimeout(alert.timer);
  };

  handleMouseOut = alert => {
    const self = this;
    alert.timer = setTimeout(() => self.hideAlert(alert), 2000);
  };

  render() {
    return (
      <div id="alert-list-container" role="alertdialog" aria-label="alert-list">
        {this.state.alerts.map((alert, i) => (
          <Alert
            key={`alert-${i}`}
            className={alert.adding ? "alert-in" : alert.hiding ? "alert-out" : ""}
            onMouseOver={() => this.handleMouseOver(alert)}
            onMouseOut={() => this.handleMouseOut(alert)}
            variant={alert.type}
            title={alert.type}
            isInline
            actionClose={
              <AlertActionCloseButton
                aria-label="alert-close-button"
                onClose={() => this.hideAlert(alert)}
              />
            }
          >
            {alert.message.length > 40
              ? `${alert.message.substr(0, 40)}...`
              : alert.message}
          </Alert>
        ))}
      </div>
    );
  }
}

export default AlertList;
