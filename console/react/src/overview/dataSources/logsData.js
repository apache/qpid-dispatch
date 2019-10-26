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
class LogRecords extends React.Component {
  detailClick = () => {
    this.props.detailClick(this.props.value, this.props.extraInfo);
  };
  render() {
    if (
      this.props.extraInfo.rowData.enable.title !== "" &&
      this.props.value !== "0"
    ) {
      return (
        <Button className="link-button" onClick={this.detailClick}>
          {this.props.value}
        </Button>
      );
    } else {
      return this.props.value;
    }
  }
}

class LogsData {
  constructor(service) {
    this.service = service;
    this.fields = [
      { title: "Router", field: "node" },
      { title: "Enable", field: "enable" },
      { title: "Module", field: "name" },
      {
        title: "Info",
        field: "infoCount",
        numeric: true,
        formatter: LogRecords
      },
      {
        title: "Trace",
        field: "traceCount",
        numeric: true,
        formatter: LogRecords
      },
      {
        title: "Debug",
        field: "debugCount",
        numeric: true,
        formatter: LogRecords
      },
      {
        title: "Notice",
        field: "noticeCount",
        numeric: true,
        formatter: LogRecords
      },
      {
        title: "Warning",
        field: "warningCount",
        numeric: true,
        formatter: LogRecords
      },
      {
        title: "Error",
        field: "errorCount",
        numeric: true,
        formatter: LogRecords
      },
      {
        title: "Critical",
        field: "criticalCount",
        numeric: true,
        formatter: LogRecords
      }
    ];
    this.detailEntity = "log";
    this.detailName = "Log";
    this.detailPath = "/logs";
    this.detailFormatter = true;
  }

  fetchRecord = (currentRecord, schema) => {
    return new Promise(resolve => {
      this.service.management.topology.fetchEntities(
        currentRecord.nodeId,
        { entity: "logStats" },
        data => {
          const record = data[currentRecord.nodeId]["logStats"];
          const identityIndex = record.attributeNames.indexOf("name");
          const result = record.results.find(
            r => r[identityIndex] === currentRecord.name
          );
          let obj = this.service.utilities.flatten(
            record.attributeNames,
            result
          );
          obj = this.service.utilities.formatAttributes(
            obj,
            schema.entityTypes["logStats"]
          );
          resolve(obj);
        }
      );
    });
  };

  doFetch = (page, perPage) => {
    return new Promise(resolve => {
      // an array of logStat records that have router name and log.enable added
      let logModules = [];
      const insertEnable = (record, logData) => {
        // find the logData result for this record
        const moduleIndex = logData.attributeNames.indexOf("module");
        const enableIndex = logData.attributeNames.indexOf("enable");
        const logRec = logData.results.find(
          r => r[moduleIndex] === record.name
        );
        if (logRec) {
          record.enable =
            logRec[enableIndex] === null ? "" : String(logRec[enableIndex]);
        } else {
          record.enable = "";
        }
      };
      this.service.management.topology.fetchAllEntities(
        [{ entity: "log" }, { entity: "logStats" }],
        nodes => {
          // each router is a node in nodes
          for (let node in nodes) {
            const nodeName = this.service.utilities.nameFromId(node);
            let response = nodes[node]["logStats"];
            // response is an array of records for this node/router
            response.results.forEach(result => {
              // result is a single log record for this router
              let logStat = this.service.utilities.flatten(
                response.attributeNames,
                result
              );

              logStat.node = nodeName;
              logStat.nodeId = node;
              insertEnable(logStat, nodes[node]["log"]);
              logModules.push(logStat);
            });
          }
          resolve({
            data: logModules,
            page,
            perPage
          });
        }
      );
    });
  };
}

export default LogsData;
