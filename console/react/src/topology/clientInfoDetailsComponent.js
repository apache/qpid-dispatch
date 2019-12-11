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

class DetailsTable extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      columns: ["Link type", "Addr", "Settle rate", "Delayed1", "Delayed10", "Usage"]
    };
  }

  render() {
    const { columns, subRows } = this.props;
    return (
      <Table aria-label="client-info-details-table" cells={columns} rows={subRows}>
        <TableHeader />
        <TableBody />
      </Table>
    );
  }
}

export default DetailsTable;
