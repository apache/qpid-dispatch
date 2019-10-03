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

import { SortByDirection } from "@patternfly/react-table";

class BaseHelper {
  constructor(service) {
    this.service = service;
    this.fields = [];
  }

  field2Row = field => ({
    cells: this.fields.map(f => field[f.field])
  });

  slice = (fields, page, perPage, sortBy) => {
    let rows = fields.map(f => this.field2Row(f));
    rows = this.sort(rows, sortBy.index, sortBy.direction);
    const total = rows.length;
    const newPages = Math.ceil(total / perPage);
    page = Math.min(page, newPages);
    const start = perPage * (page - 1);
    const end = Math.min(start + perPage, rows.length);
    const slicedRows = rows.slice(start, end);
    return { rows: slicedRows, page, total, allRows: rows };
  };

  sort = (rows, index, direction) => {
    if (typeof index === "undefined" || typeof direction === "undefined") {
      return rows;
    }
    rows.sort((a, b) =>
      a.cells[index] < b.cells[index]
        ? -1
        : a.cells[index] > b.cells[index]
        ? 1
        : 0
    );
    if (direction === SortByDirection.desc) {
      rows = rows.reverse();
    }
    return rows;
  };
}

export default BaseHelper;
