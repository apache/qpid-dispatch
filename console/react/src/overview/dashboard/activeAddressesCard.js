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
import { nowrap, Table, TableBody, TableHeader } from "@patternfly/react-table";

// update the table every 5 seconds
const UPDATE_INTERVAL = 1000 * 5;

class ActiveAddressesCard extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      lastUpdate: new Date(),
      columns: ["Address", "In", "Out", "Settle rate"].map(it => ({
        title: it,
        transforms: [nowrap],
      })),
      rows: [],
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
      [
        {
          entity: "router.link",
          attrs: ["settleRate", "linkType", "linkDir", "owningAddr"],
        },
        {
          entity: "router.address",
          attrs: ["identity", "deliveriesIngress", "deliveriesEgress"],
        },
      ],
      results => {
        if (!this.mounted) return;
        let active = {};
        for (let id in results) {
          const linkData = results[id]["router.link"];
          const addressData = results[id]["router.address"];
          const addresses = this.props.service.utilities.flattenAll(addressData);

          for (let i = 0; i < linkData.results.length; i++) {
            const link = this.props.service.utilities.flatten(
              linkData.attributeNames,
              linkData.results[i]
            );
            if (link.linkType === "endpoint") {
              if (link.owningAddr && !link.owningAddr.startsWith("Ltemp.")) {
                if (!active[link.owningAddr]) {
                  active[link.owningAddr] = {
                    addr: this.props.service.utilities.addr_text(link.owningAddr),
                    in: 0,
                    out: 0,
                    settleRate: 0,
                  };
                }
                const address = addresses.find(
                  address => address.identity === link.owningAddr
                );
                if (address) {
                  if (link.linkDir === "in") {
                    active[link.owningAddr].in += parseInt(address.deliveriesIngress);
                  }
                  if (link.linkDir === "out") {
                    active[link.owningAddr].out += parseInt(address.deliveriesEgress);
                    active[link.owningAddr].settleRate += parseInt(link.settleRate);
                  }
                }
              }
            }
          }
        }
        const activeKeys = Object.keys(active).filter(
          key => active[key].in > 0 && active[key].out > 0
        );
        const rows = activeKeys.map(addr => {
          return {
            cells: [
              active[addr].addr,
              active[addr].in.toLocaleString(),
              active[addr].out.toLocaleString(),
              active[addr].settleRate.toLocaleString(),
            ],
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
        <div className="updated">Updated at {this.lastUpdateString()}</div>
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
