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
import {
  sortable,
  SortByDirection,
  Table,
  TableHeader,
  TableBody,
  TableVariant
} from "@patternfly/react-table";
import { Button, Pagination } from "@patternfly/react-core";
import { Navigate } from "react-router-dom";
import TableToolbar from "../common/tableToolbar";
import { dataMap } from "./entityData";

// If the breadcrumb on the details page was used to return to this page,
// we will have saved state info in props.location.state
const propFromLocation = (props, which, defaultValue) =>
  props &&
  props.location &&
  props.location.state &&
  typeof props.location.state[which] !== "undefined"
    ? props.location.state[which]
    : defaultValue;

class OverviewTable extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      sortBy: propFromLocation(props, "sortBy", {
        index: 0,
        direction: SortByDirection.asc
      }),
      filterBy: propFromLocation(props, "filterBy", {}),
      perPage: propFromLocation(props, "perPage", 10),
      total: 1,
      page: propFromLocation(props, "page", 1),
      columns: [],
      allRows: [],
      rows: [],
      redirect: false,
      redirectState: {}
    };
    this.entity = this.props.service.utilities.entityFromProps(props);
    if (!dataMap[this.entity]) {
      this.state.redirect = true;
    } else {
      this.dataSource = new dataMap[this.entity](this.props.service);
    }
  }

  componentDidMount = () => {
    this.mounted = true;
    if (!this.dataSource) return;
    // initialize the columns and get the data
    this.dataSource.fields.forEach(f => {
      f.transforms = [];
      f.cellFormatters = [];
      if (!f.noSort) f.transforms.push(sortable);
      if (f.numeric) {
        f.cellFormatters.push(this.prettier);
      }
      if (f.noWrap) {
        f.cellFormatters.push(this.noWrap);
      }
      if (f.formatter) {
        f.cellFormatters.push((value, extraInfo) =>
          this.formatter(f.formatter, value, extraInfo)
        );
      }
    });
    // if the dataSource did not provide its own cell formatter for details
    if (!this.dataSource.detailFormatter) {
      this.dataSource.fields[0].cellFormatters.push(this.detailLink);
    }

    this.setState({ columns: this.dataSource.fields }, () => {
      this.update();
      this.timer = setInterval(this.update, 5000);
    });
  };

  componentWillUnmount = () => {
    this.mounted = false;
    clearInterval(this.timer);
  };

  update = () => {
    this.fetch(this.state.page, this.state.perPage);
  };

  fetch = (page, perPage) => {
    // get the data. Note: The current page number might change if
    // the number of rows is less than before
    this.dataSource.doFetch(page, perPage).then(results => {
      const sliced = this.slice(results.data, results.page, results.perPage);
      // if fetch was called and the component was unmounted before
      // the results arrived, don't call setState
      if (!this.mounted) return;
      const { rows, page, total, allRows } = sliced;
      if (this.mounted)
        this.setState({
          rows,
          page,
          perPage,
          total,
          allRows
        });
      this.props.lastUpdated(new Date());
    });
  };

  detailLink = (value, extraInfo) => {
    return (
      <Button className="link-button" onClick={() => this.detailClick(value, extraInfo)}>
        {value}
      </Button>
    );
  };

  detailClick = (value, extraInfo) => {
    this.setState({
      redirect: true,
      redirectState: {
        value: extraInfo.rowData.cells[extraInfo.columnIndex],
        currentRecord: extraInfo.rowData.data,
        entity: this.entity,
        page: this.state.page,
        sortBy: this.state.sortBy,
        filterBy: this.state.filterBy,
        perPage: this.state.perPage,
        property: extraInfo.property
      }
    });
  };

  // cell formatter
  noWrap = (value, extraInfo) => {
    return <span className="noWrap">{value}</span>;
  };

  // cell formatter
  prettier = (value, extraInfo) => {
    return typeof value === "undefined"
      ? "-"
      : this.props.service.utilities.pretty(value);
  };

  // cell formatter, display a component instead of this cell's data
  formatter = (Component, value, extraInfo) => {
    return (
      <Component
        value={value}
        extraInfo={extraInfo}
        service={this.props.service}
        detailClick={this.detailClick}
        handleAddNotification={this.props.handleAddNotification}
      />
    );
  };

  onSort = (_event, index, direction) => {
    this.setState({ sortBy: { index, direction } }, () => {
      const { allRows, page, perPage } = this.state;
      let rows = this.filter(allRows);
      rows = this.sort(rows);
      rows = this.page(rows, rows.length, page, perPage);
      this.setState({ rows });
    });
  };

  renderPagination(variant = "top") {
    const { page, perPage, total } = this.state;
    return (
      <Pagination
        itemCount={total}
        page={page}
        perPage={perPage}
        onSetPage={(_evt, value) => this.onSetPage(value)}
        onPerPageSelect={(_evt, value) => this.onPerPageSelect(value)}
        variant={variant}
      />
    );
  }

  onSetPage = value => {
    this.fetch(value, this.state.perPage);
  };
  onPerPageSelect = value => {
    this.fetch(1, value);
  };
  handleChangeFilterValue = (field, value) => {
    this.setState({ filterBy: { field, value } }, this.update);
  };

  field2Row = field => ({
    cells: this.dataSource.fields.map(f => field[f.field]),
    data: field
  });

  cellIndex = field => {
    return this.dataSource.fields.findIndex(f => {
      return f.title === field;
    });
  };

  filter = rows => {
    const filterField = this.state.filterBy.field;
    const filterValue = this.state.filterBy.value;
    if (
      typeof filterField !== "undefined" &&
      typeof filterValue !== "undefined" &&
      filterValue !== ""
    ) {
      const cellIndex = this.cellIndex(filterField);
      rows = rows.filter(r => {
        return r.cells[cellIndex].includes(filterValue);
      });
    }
    return rows;
  };

  page = (rows, total, page, perPage) => {
    const newPages = Math.ceil(total / perPage);
    page = Math.min(page, newPages);
    const start = perPage * (page - 1);
    const end = Math.min(start + perPage, rows.length);
    return rows.slice(start, end);
  };

  slice = (fields, page, perPage) => {
    let allRows = fields.map(f => this.field2Row(f));
    let rows = this.filter(allRows);
    const total = rows.length;
    rows = this.sort(rows);
    rows = this.page(rows, total, page, perPage);
    return { rows, page, total, allRows };
  };

  sort = rows => {
    const { index, direction } = this.state.sortBy;
    if (typeof index === "undefined" || typeof direction === "undefined") {
      return rows;
    }

    const less = direction === SortByDirection.desc ? 1 : -1;
    const more = -1 * less;
    rows.sort((a, b) => {
      if (a.cells[index] < b.cells[index]) return less;
      if (a.cells[index] > b.cells[index]) return more;
      // the values matched, sort by 1st column
      if (index !== 0) {
        if (a.cells[0] < b.cells[0]) return less;
        if (a.cells[0] > b.cells[0]) return more;
      }
      return 0;
    });
    return rows;
  };

  render() {
    if (this.state.redirect) {
      return (
        <Navigate
          to={(this.dataSource && this.dataSource.detailPath) || "/details"}
          state={this.state.redirectState}
        />
      );
    }
    return (
      <React.Fragment>
        <TableToolbar
          total={this.state.total}
          page={this.state.page}
          perPage={this.state.perPage}
          onSetPage={this.onSetPage}
          onPerPageSelect={this.onPerPageSelect}
          fields={this.dataSource.fields}
          handleChangeFilterValue={this.handleChangeFilterValue}
        />
        <Table
          cells={this.state.columns}
          rows={this.state.rows}
          aria-label={this.entity}
          sortBy={this.state.sortBy}
          onSort={this.onSort}
          variant={TableVariant.compact}
        >
          <TableHeader />
          <TableBody />
        </Table>
        {this.renderPagination("bottom")}
      </React.Fragment>
    );
  }
}

export default OverviewTable;
