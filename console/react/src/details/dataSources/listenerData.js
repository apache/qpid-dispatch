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
import DefaultData from "./defaultData";

const HostPort = ({ value, extraInfo }) => {
  const host = extraInfo.rowData.data.host;
  const port = extraInfo.rowData.data.port;
  return <span className="entity-type">{`${host}:${port}`}</span>;
};

class ListenerData extends DefaultData {
  constructor(service, schema) {
    super(service, schema);
    this.service = service;
    this.extraFields = [
      { title: "Role", field: "role" },
      {
        title: "Host:Port",
        field: "host",
        formatter: HostPort,
        filter: this.hostPortFilter
      }
    ];
    this.detailEntity = "listener";
  }

  hostPortFilter = (data, filterValue) => {
    const hostport = `${data.host}${data.port}`;
    return hostport.includes(filterValue);
  };
}

export default ListenerData;
