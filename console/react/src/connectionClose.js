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
import { Button } from "@patternfly/react-core";

class ConnectionClose extends React.Component {
  closeConnection = () => {
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
        let statusCode =
          results.context.message.application_properties.statusCode;
        if (statusCode < 200 || statusCode >= 300) {
          console.log(
            `error ${record.name} ${results.context.message.application_properties.statusDescription}`
          );
        } else {
          console.log(
            `success ${record.name} ${results.context.message.application_properties.statusDescription}`
          );
        }
      });
  };

  render() {
    if (this.props.extraInfo.rowData.data.role === "normal") {
      return (
        <Button className="link-button" onClick={this.closeConnection}>
          Close
        </Button>
      );
    } else {
      return <React.Fragment />;
    }
  }
}

export default ConnectionClose;
