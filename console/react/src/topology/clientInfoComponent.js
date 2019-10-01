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
import { Modal } from "@patternfly/react-core";
import {
  Table,
  TableHeader,
  TableBody,
  TableVariant,
  compoundExpand
} from "@patternfly/react-table";
import { CodeBranchIcon } from "@patternfly/react-icons";

import DetailsTable from "./clientInfoDetailsComponent";
import { utils } from "../amqp/utilities.js";
const { queue } = require("d3-queue");

class ClientInfoComponent extends Component {
  constructor(props) {
    super(props);
    this.state = {
      toolTip: null,
      detail: null,
      columns: [
        "Container",
        "Encrypted",
        "Host",
        {
          title: "Links",
          cellTransforms: [compoundExpand]
        }
      ],
      rows: [
        {
          isOpen: false,
          cells: [
            {
              title: <span>container</span>,
              props: { component: "th" }
            },
            {
              title: <span>False</span>
            },
            {
              title: <span>host</span>
            },
            {
              title: (
                <React.Fragment>
                  <CodeBranchIcon key="icon" /> 1
                </React.Fragment>
              ),
              props: {
                isOpen: false,
                ariaControls: "compound-expansion-table-1"
              }
            }
          ]
        },
        {
          parent: 0,
          compoundParent: 3,
          cells: [
            {
              title: (
                <DetailsTable
                  rows={[1, 2, 3, 4, 5, 6]}
                  id="compound-expansion-table-1"
                />
              ),
              props: { colSpan: 4, className: "pf-m-no-padding" }
            }
          ]
        }
      ]
    };
    this.timer = null;
    this.rates = {};
    this.expandedRows = new Set();
    this.d = this.props.d; // the node object

    this.dStart = 0;
    this.dStop = Math.min(this.d.normals.length, 10);
    this.cachedInfo = [];
    this.updateTimer = null;

    // which attributes to fetch and display
    this.fields = {
      detailFields: {
        cols: [
          "version",
          "mode",
          "presettledDeliveries",
          "droppedPresettledDeliveries",
          "acceptedDeliveries",
          "rejectedDeliveries",
          "releasedDeliveries",
          "modifiedDeliveries",
          "deliveriesIngress",
          "deliveriesEgress",
          "deliveriesTransit",
          "deliveriesIngressRouteContainer",
          "deliveriesEgressRouteContainer"
        ]
      },
      linkFields: {
        attrs: [
          "linkType",
          "owningAddr",
          "settleRate",
          "deliveriesDelayed1Sec",
          "deliveriesDelayed10Sec",
          "unsettledCount",
          "capacity"
        ],
        cols: [
          "linkType",
          "addr",
          "settleRate",
          "delayed1",
          "delayed10",
          "usage"
        ],
        calc: {
          addr: link => {
            return utils.addr_text(link.owningAddr);
          },
          delayed1: link => {
            return link.deliveriesDelayed1Sec;
          },
          delayed10: link => {
            return link.deliveriesDelayed10Sec;
          },
          usage: link => {
            return link.unsettledCount / link.capacity;
          }
        }
      },
      linkRouteFields: {
        cols: ["prefix", "direction", "containerId"]
      },
      autoLinkFields: {
        cols: ["addr", "direction", "containerId"]
      },
      addressFields: {
        cols: ["prefix", "distribution"]
      }
    };
  }

  componentDidMount = () => {
    this.timer = setInterval(this.getTooltip, 5000);
    this.getTooltip();
    this.doUpdateDetail();
  };

  componentWillUnmount = () => {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
    if (this.updateTimer) {
      clearTimeout(this.updateTimer);
      this.updateTimer = null;
    }
  };
  getTooltip = () => {
    this.props.d.toolTip(this.props.topology, true).then(toolTip => {
      this.setState({ toolTip });
    });
  };

  // called for each expanded row to get further details about the edge router
  moreInfo = (id, infoPerId) => {
    let nodeId = utils.idFromName(id, "_edge");
    this.props.topology.fetchEntities(
      nodeId,
      [
        { entity: "router.link", attrs: [] },
        {
          entity: "linkRoute",
          attrs: this.fields.linkRouteFields.cols
        },
        {
          entity: "autoLink",
          attrs: this.fields.autoLinkFields.cols
        },
        { entity: "address", attrs: [] }
      ],
      results => {
        // save the results for each entity requested
        if (infoPerId[id]) {
          infoPerId[id].linkRoutes = utils.flattenAll(
            results[nodeId].linkRoute
          );
          infoPerId[id].autoLinks = utils.flattenAll(results[nodeId].autoLink);
          infoPerId[id].addresses = utils.flattenAll(results[nodeId].address);
        }
      }
    );
  };

  // get the detail info for the popup
  groupDetail = () => {
    // queued function to get the .router info for an edge router
    const q_getEdgeInfo = (n, infoPerId, callback) => {
      const nodeId = utils.idFromName(n.container, "_edge");
      this.props.topology.fetchEntities(
        nodeId,
        [{ entity: "router", attrs: [] }],
        results => {
          let r = results[nodeId].router;
          infoPerId[n.container] = utils.flatten(
            r.attributeNames,
            r.results[0]
          );
          let rates = utils.rates(
            infoPerId[n.container],
            ["acceptedDeliveries"],
            this.rates,
            n.container,
            1
          );
          infoPerId[n.container].acceptedDeliveriesRate = Math.round(
            rates.acceptedDeliveries,
            2
          );
          infoPerId[n.container].linkRoutes = [];
          infoPerId[n.container].autoLinks = [];
          infoPerId[n.container].addresses = [];
          callback(null);
        }
      );
    };
    return new Promise(resolve => {
      let infoPerId = {};
      // we are getting info for an edge router
      if (this.d.nodeType === "edge") {
        // async send up to 10 requests
        let q = queue(10);
        for (let n = this.dStart; n < this.dStop; n++) {
          q.defer(q_getEdgeInfo, this.d.normals[n], infoPerId);
          if (this.expandedRows.has(this.d.normals[n].container)) {
            this.moreInfo(this.d.normals[n].container, infoPerId);
          }
        }
        // await until all sent requests have completed
        q.await(() => {
          this.setState({
            detail: { template: "edgeRouters", title: "edge router" }
          });
          // send the results
          resolve({
            description: "Select an edge router to see more info",
            infoPerId: infoPerId
          });
        });
      } else {
        // we are getting info for a group of clients or consoles
        let attrs = utils.copy(this.fields.linkFields.attrs);
        attrs.unshift("connectionId");
        this.props.topology.fetchEntities(
          this.d.key,
          [{ entity: "router.link", attrs: attrs }],
          results => {
            let links = results[this.d.key]["router.link"];
            for (let i = 0; i < this.d.normals.length; i++) {
              let n = this.d.normals[i];
              let conn = {};
              infoPerId[n.container] = conn;
              conn.container = n.container;
              conn.encrypted = n.encrypted ? "True" : "False";
              conn.host = n.host;
              conn.links = utils.flattenAll(links, link => {
                this.fields.linkFields.cols.forEach(col => {
                  if (this.fields.linkFields.calc[col]) {
                    link[col] = this.fields.linkFields.calc[col](link);
                  }
                });
                if (link.connectionId === n.connectionId) {
                  link.owningAddr = utils.addr_text(link.owningAddr);
                  link.addr = link.owningAddr;
                  return link;
                } else {
                  return null;
                }
              });
              conn.linkCount = conn.links.length;
            }
            let dir =
              this.d.cdir === "in"
                ? "inbound"
                : this.d.cdir === "both"
                ? "in and outbound"
                : "outbound";
            let count = this.d.normals.length;
            let verb = count > 1 ? "are" : "is";
            let preposition =
              this.d.cdir === "in"
                ? "to"
                : this.d.cdir === "both"
                ? "for"
                : "from";
            let plural = count > 1 ? "s" : "";
            this.setState({
              detail: { template: "clients", title: "client" }
            });
            resolve({
              description: `There ${verb} ${count} ${dir} connection${plural} ${preposition} ${this.d.routerId} with role ${this.d.nodeType}`,
              infoPerId: infoPerId
            });
          }
        );
      }
    });
  };

  doUpdateDetail = () => {
    this.cachedInfo = [];
    this.updateDetail();
  };
  updateDetail = () => {
    this.groupDetail().then(det => {
      Object.keys(det.infoPerId).forEach(id => {
        this.cachedInfo.push(det.infoPerId[id]);
      });
      if (this.dStop < this.d.normals.length) {
        this.dStart = this.dStop;
        this.dStop = Math.min(this.d.normals.length, this.dStart + 10);
        setTimeout(this.updateDetail, 1);
      } else {
        const infoPerId = this.cachedInfo.sort((a, b) => {
          return a.name > b.name ? 1 : -1;
        });
        const rows = this.getRows(infoPerId);
        this.setState({
          detail: {
            title: `for ${this.d.normals.length} ${this.state.detail.title}${
              this.d.normals.length > 1 ? "s" : ""
            }`,
            description: det.description,
            infoPerId: infoPerId
          },
          rows: rows
        });
        this.dStart = 0;
        this.dStop = Math.min(this.d.normals.length, 10);
        this.updateTimer = setTimeout(this.doUpdateDetail, 2000);
      }
    });
  };

  // load the columns array from infoPerId
  getRows = infoPerId => {
    let newRows = [];
    const oldRows = this.state.rows;
    for (let i = 0; i < infoPerId.length; i++) {
      let row = infoPerId[i];
      let oldRow = oldRows.find(r => r.cells[0].title === row.container);
      let cells = [];
      cells.push({ title: row.container, props: { component: "th" } });
      cells.push({ title: row.encrypted });
      cells.push({ title: row.host });
      cells.push({
        title: (
          <React.Fragment>
            <CodeBranchIcon key="icon" /> {row.links.length}
          </React.Fragment>
        ),
        props: {
          isOpen: oldRow ? oldRow.cells[3].props.isOpen : false,
          ariaControls: "compound-expansion-table-1"
        }
      });
      newRows.push({
        isOpen: oldRow ? oldRow.isOpen : false,
        cells: cells
      });
      let subRows = [];
      row.links.forEach(link => {
        let subCells = [];
        this.fields.linkFields.cols.forEach(col => {
          subCells.push(link[col]);
        });
        subRows.push({ cells: subCells });
      });

      newRows.push({
        parent: i * 2,
        compoundParent: 3,
        cells: [
          {
            title: (
              <DetailsTable
                rows={subRows}
                subRows={subRows}
                tst={`there are ${subRows.length} sub rows`}
                id="compound-expansion-table-1"
              />
            ),
            props: { colSpan: 4, className: "pf-m-no-padding" }
          }
        ]
      });
    }
    return newRows;
  };

  /*
  {
    isOpen: false,
    cells: [
      {
        title: <span>container</span>,
        props: { component: "th" }
      },
      {
        title: <span>False</span>
      },
      {
        title: <span>host</span>
      },
      {
        title: (
          <React.Fragment>
            <CodeBranchIcon key="icon" /> 1
          </React.Fragment>
        ),
        props: {
          isOpen: false,
          ariaControls: "compound-expansion-table-1"
        }
      }
    ]
  },
  {
    parent: 0,
    compoundParent: 3,
    cells: [
      {
        title: (
          <DetailsTable
            firstColumnRows={[
              "parent-0",
              "compound-1",
              "three",
              "four",
              "five"
            ]}
            id="compound-expansion-table-1"
          />
        ),
        props: { colSpan: 4, className: "pf-m-no-padding" }
      }
    ]
  }
*/

  onExpand = (event, rowIndex, colIndex, isOpen, rowData, extraData) => {
    const { rows } = this.state;
    if (!isOpen) {
      //set all other expanded cells false in this row if we are expanding
      rows[rowIndex].cells.forEach(cell => {
        if (cell.props) cell.props.isOpen = false;
      });
      rows[rowIndex].cells[colIndex].props.isOpen = true;
      rows[rowIndex].isOpen = true;
    } else {
      rows[rowIndex].cells[colIndex].props.isOpen = false;
      rows[rowIndex].isOpen = rows[rowIndex].cells.some(
        cell => cell.props && cell.props.isOpen
      );
    }
    this.setState({
      rows
    });
  };

  render() {
    const { detail } = this.state;
    const { columns, rows } = this.state;
    return (
      <Modal
        isSmall
        title={`Details ${detail ? detail.title : ""}`}
        isOpen={true}
        onClose={this.props.handleCloseClientInfo}
      >
        {rows && detail ? (
          <Table
            caption={detail.description}
            variant={TableVariant.compact}
            onExpand={this.onExpand}
            borders={false}
            rows={rows}
            cells={columns}
          >
            <TableHeader />
            <TableBody />
          </Table>
        ) : (
          <div>Loading...</div>
        )}
      </Modal>
    );
  }
}

export default ClientInfoComponent;
