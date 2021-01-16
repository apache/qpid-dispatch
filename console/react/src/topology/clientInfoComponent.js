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
import { Button, Modal, ModalVariant } from "@patternfly/react-core";
import {
  Table,
  TableHeader,
  TableBody,
  TableVariant,
  compoundExpand
} from "@patternfly/react-table";
import { CodeBranchIcon } from "@patternfly/react-icons";

import DetailsTable from "./clientInfoDetailsComponent";
import { utils } from "../common/amqp/utilities.js";
const { queue } = require("d3-queue");

const PERPAGE = 10;
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
              title: "",
              props: { colSpan: 4, className: "pf-m-no-padding" }
            }
          ]
        }
      ]
    };
    this.rates = {};
    this.d = this.props.d; // the node object

    this.dStart = 0;
    this.dStop = Math.min(this.d.normals.length, PERPAGE);
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
        cols: ["linkType", "addr", "settleRate", "delayed1", "delayed10", "usage"],
        columns: ["Link type", "Addr", "Settle rate", "Delayed1", "Delayed10", "Usage"],
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
        cols: ["name", "distribution", "deliveriesEgress"]
      },
      clients: [
        "Container",
        "Encrypted",
        "Host",
        {
          title: "Links",
          cellTransforms: [compoundExpand]
        }
      ],
      edgeRouters: [
        "Name",
        "Connections",
        "Accepted rate",
        {
          title: "Addresses",
          cellTransforms: [compoundExpand]
        },
        ""
      ],
      edgeColumns: ["Name", "Distribution", "Deliveries egress"]
    };
  }

  componentDidMount = () => {
    this.doUpdateDetail();
  };

  componentWillUnmount = () => {
    this.unmounted = true;

    if (this.updateTimer) {
      clearTimeout(this.updateTimer);
      this.updateTimer = null;
    }
  };

  handleSeparate = row => {
    this.props.handleSeparate(row.container).then(d => {
      if (!d || !d.normals) {
        this.props.handleCloseClientInfo();
      } else {
        this.d = d;
        this.dStart = 0;
        this.dStop = Math.min(this.d.normals.length, PERPAGE);
        if (this.updateTimer) {
          clearTimeout(this.updateTimer);
          this.updateTimer = null;
        }
        this.doUpdateDetail();
      }
    });
  };

  // get the detail info for the popup
  groupDetail = () => {
    // queued function to get the .router info for an edge router
    const q_getEdgeInfo = (n, infoPerId, resolve) => {
      const nodeId = utils.idFromName(n.container, "_edge");
      this.props.topology.fetchEntities(
        nodeId,
        [
          { entity: "router" },
          { entity: "router.address", attrs: this.fields.addressFields }
        ],
        results => {
          let r = results[nodeId].router;
          infoPerId[n.container] = utils.flatten(r.attributeNames, r.results[0]);
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
          ).toLocaleString();
          infoPerId[n.container].container = n.container;
          infoPerId[n.container].addresses = utils.flattenAll(
            results[nodeId]["router.address"],
            address => {
              address.deliveriesEgress = parseInt(
                address.deliveriesEgress
              ).toLocaleString();
              return address;
            }
          );
          resolve(null);
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
        }
        // await until all sent requests have completed
        q.await(() => {
          if (this.unmounted) return;
          const columns = this.fields.edgeRouters;
          this.setState({
            detail: { template: "edgeRouters", title: "edge router" },
            columns
          });
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
            if (this.unmounted) return;
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
              this.d.cdir === "in" ? "to" : this.d.cdir === "both" ? "for" : "from";
            let plural = count > 1 ? "s" : "";
            const columns = this.fields.clients;
            this.setState({
              detail: { template: "clients", title: "client" },
              columns
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
      if (this.unmounted) return;
      Object.keys(det.infoPerId).forEach(id => {
        this.cachedInfo.push(det.infoPerId[id]);
      });
      /*
      if (this.dStop < this.d.normals.length) {
        this.dStart = this.dStop;
        this.dStop = Math.min(this.d.normals.length, this.dStart + PERPAGE);
        setTimeout(this.updateDetail, 1);
      } else {
        */
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
      this.dStop = Math.min(this.d.normals.length, PERPAGE);
      this.updateTimer = setTimeout(this.doUpdateDetail, 2000);
      //}
    });
  };

  // load the columns array from infoPerId
  getRows = infoPerId => {
    let newRows = [];
    const oldRows = this.state.rows;
    const limit = Math.min(PERPAGE, infoPerId.length);
    for (let i = 0; i < limit; i++) {
      let row = infoPerId[i];
      let oldRow;
      let cells = [];
      let subRows = [];
      let columns = [];
      let colSpan = 4;

      if (this.state.detail.template === "edgeRouters") {
        // infoPerId is an array of router info
        columns = this.fields.edgeColumns;
        colSpan = 5;
        oldRow = oldRows.find(r => r.cells[0].title === row.name);
        cells.push({ title: row.name, props: { component: "th" } });
        cells.push({ title: row.connectionCount });
        cells.push({ title: row.acceptedDeliveriesRate });
        cells.push({
          title: (
            <React.Fragment>
              <CodeBranchIcon key="icon" /> {row.addresses.length}{" "}
            </React.Fragment>
          ),
          props: {
            isOpen: oldRow ? oldRow.cells[3].props.isOpen : false,
            ariaControls: "compound-expansion-table-1"
          }
        });
        cells.push({
          title: (
            <Button
              className="link-button"
              variant="link"
              onClick={() => this.handleSeparate(row)}
            >
              Expand
            </Button>
          )
        });
        newRows.push({
          isOpen: oldRow ? oldRow.isOpen : false,
          cells: cells
        });
        row.addresses.forEach(address => {
          let subCells = [];
          this.fields.addressFields.cols.forEach(col => {
            subCells.push(address[col]);
          });
          subRows.push({ cells: subCells });
        });
      } else {
        // infoPerId is an array of connections
        columns = this.fields.linkFields.columns;
        oldRow = oldRows.find(r => r.cells[0].title === row.container);
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
        row.links.forEach(link => {
          let subCells = [];
          this.fields.linkFields.cols.forEach(col => {
            subCells.push(link[col]);
          });
          subRows.push({ cells: subCells });
        });
      }
      newRows.push({
        parent: i * 2,
        compoundParent: 3,
        cells: [
          {
            title: (
              <DetailsTable
                columns={columns}
                subRows={subRows}
                id="compound-expansion-table-1"
              />
            ),
            props: { colSpan, className: "pf-m-no-padding" }
          }
        ]
      });
    }
    return newRows;
  };

  onExpand = (event, rowIndex, colIndex, isOpen, rowData, extraData) => {
    const { rows } = this.state;
    if (this.unmounted) return;
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
        variant={ModalVariant.small}
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
            aria-label="client-info-table"
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
