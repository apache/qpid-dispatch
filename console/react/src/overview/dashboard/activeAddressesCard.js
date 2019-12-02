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
import { Table, TableHeader, TableBody } from "@patternfly/react-table";

// update the table every 5 seconds
const UPDATE_INTERVAL = 1000 * 5;

class ActiveAddressesCard extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      lastUpdate: new Date(),
      columns: ["Address", "Class", "Settle rate"],
      rows: []
    };
  }

  componentDidMount = () => {
    this.mounted = true;
    this.timer = setInterval(this.updateData, UPDATE_INTERVAL);
    this.updateData();
  };

  componentWillUnmount = () => {
    this.mounted = false;
    clearInterval(this.timer);
  };

  updateData = () => {
    this.props.service.management.topology.fetchAllEntities(
      {
        entity: "router.link",
        attrs: ["settleRate", "linkType", "linkDir", "owningAddr"]
      },
      results => {
        if (!this.mounted) return;
        let active = {};
        for (let id in results) {
          const aresult = results[id]["router.link"];
          for (let i = 0; i < aresult.results.length; i++) {
            const result = this.props.service.utilities.flatten(
              aresult.attributeNames,
              aresult.results[i]
            );
            if (result.linkType === "endpoint" && result.linkDir === "in") {
              if (
                parseInt(result.settleRate) > 0 &&
                result.owningAddr && !result.owningAddr.startsWith("Ltemp.")
              ) {
                if (!active.hasOwnProperty[result.owningAddr]) {
                  active[result.owningAddr] = {
                    addr: this.props.service.utilities.addr_text(result.owningAddr),
                    cls: this.props.service.utilities.addr_class(result.owningAddr),
                    settleRate: 0
                  };
                }
                active[result.owningAddr].settleRate += parseInt(result.settleRate);
              }
            }
          }
        }
        let rows = Object.keys(active).map(addr => {
          return {
            cells: [
              active[addr].addr,
              active[addr].cls,
              active[addr].settleRate.toLocaleString()
            ]
          };
        });
        this.setState({ rows, lastUpdate: new Date() });
      }
    );
  };

  nextUpdateString = () => {
    const nextUpdate = new Date(this.state.lastUpdate.getTime() + UPDATE_INTERVAL);
    return this.props.service.utilities.strDate(nextUpdate);
  };

  lastUpdateString = () => {
    return this.props.service.utilities.strDate(this.state.lastUpdate);
  };
  render() {
    const { columns, rows } = this.state;

    const caption = (
      <React.Fragment>
        <span className="caption">Most active addresses</span>
        <div className="updated">
          Updated at {this.lastUpdateString()} | Next {this.nextUpdateString()}
        </div>
      </React.Fragment>
    );
    return (
      <Table caption={caption} cells={columns} rows={rows}>
        <TableHeader />
        <TableBody />
      </Table>
    );
  }
}

export default ActiveAddressesCard;
