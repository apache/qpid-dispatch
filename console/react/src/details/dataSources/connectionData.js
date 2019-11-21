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
import ConnectionClose from "../../common/connectionClose";

class ConnectionData extends DefaultData {
  constructor(service, schema) {
    super(service, schema);
    this.extraFields = [
      {
        title: "",
        field: "connection",
        noSort: true,
        formatter: ConnectionClose
      }
    ];
    this.detailEntity = "router.link";
    this.detailName = "Link";
  }

  detailActions = (entity, props, record) => {
    return (
      <ConnectionClose
        asButton={true}
        extraInfo={{ rowData: { data: record } }}
        {...props}
      />
    );
  };
}

export default ConnectionData;
