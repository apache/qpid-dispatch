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
import { dataMap, defaultData } from "./entityData";
import EmptyTable from "./emptyTablePage";

// If the breadcrumb on the detailsTablePage was used to return to this page,
// we will have saved state info in props.location.state
const propFromLocation = (props, which, defaultValue) => {
  return props && props.detailsState && typeof props.detailsState[which] !== "undefined"
    ? props.detailsState[which]
    : defaultValue;
};

class EntityListTable extends React.Component {
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
      allRows: [],
      rows: [],
      redirect: false,
      redirectState: {},
      action: null,
      data: null
    };
    this.initDataSource();
    this.columns = [];
  }

  componentDidMount = () => {
    this.mounted = true;
    this.setupFields();
    this.timer = setInterval(this.update, 5000);
  };

  componentDidUpdate = prevProps => {
    if (
      prevProps.entity !== this.props.entity ||
      prevProps.routerId !== this.props.routerId
    ) {
      this.setupFields();
    }
  };
  componentWillUnmount = () => {
    this.mounted = false;
    clearInterval(this.timer);
  };

  initDataSource = () => {
    this.dataSource = dataMap[this.props.entity]
      ? new dataMap[this.props.entity](this.props.service, this.props.schema)
      : new defaultData(this.props.service, this.props.schema);
    this.dataSource.fields = [{ title: "Name", field: "name" }];
    if (this.dataSource.typeFormatter) {
      this.dataSource.fields.push({
        title: "Type",
        field: "type",
        formatter: this.dataSource.typeFormatter
      });
    }
    if (this.dataSource.extraFields) {
      this.dataSource.fields.push(...this.dataSource.extraFields);
    }
    if (this.dataSource.actionColumn) {
      this.dataSource.fields.push(this.dataSource.actionColumn);
    }
  };

  setupFields = () => {
    this.initDataSource();
    this.columns = [];
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
    this.columns = this.dataSource.fields;
    this.update();
  };

  update = () => {
    if (this.props.entity && this.props.routerId) {
      this.fetch(this.state.page, this.state.perPage);
    }
  };

  fetch = (page, perPage) => {
    // get the data. Note: The current page number might change if
    // the number of rows is less than before
    const routerId = this.props.routerId;
    const entity = this.props.entity;
    this.dataSource.doFetch(page, perPage, routerId, entity).then(results => {
      const sliced = this.slice(results.data, results.page, results.perPage);
      // if fetch was called and the component was unmounted before
      // the results arrived, don't call setState
      if (!this.mounted) return;
      const { rows, page, total, allRows } = sliced;
      allRows.forEach(row => {
        const prevRow = this.state.allRows.find(r => r.data.name === row.data.name);
        if (prevRow && prevRow.selected) {
          row.selected = true;
        }
      });
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
    if (value === null) {
      value = `${this.props.entity}/${extraInfo.rowData.data.identity}`;
    }
    return (
      <Button
        data-testid={value}
        className="link-button"
        onClick={() => this.detailClick(value, extraInfo)}
      >
        {value}
      </Button>
    );
  };

  detailClick = (value, extraInfo) => {
    const stateInfo = {
      page: this.state.page,
      perPage: this.state.perPage,
      routerId: this.props.routerId,
      entity: this.props.entity,
      filterBy: this.state.filterBy
    };
    this.props.handleDetailClick(value, extraInfo, stateInfo);
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
        detailClick={this.detailClick}
        notifyClick={this.notifyClick}
        {...this.props}
      />
    );
  };

  notifyClick = () => {
    this.update();
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
        if (this.dataSource.fields[cellIndex].filter) {
          return this.dataSource.fields[cellIndex].filter(r.data, filterValue);
        }
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

  onSelect = (event, isSelected, rowId) => {
    let rows;
    if (rowId === -1) {
      rows = this.state.rows.map(oneRow => {
        oneRow.selected = isSelected;
        return oneRow;
      });
    } else {
      rows = [...this.state.rows];
      rows[rowId].selected = isSelected;
    }
    this.setState({
      rows
    });
  };

  // called from entitiesPage when a new entity is selected from the list.
  // we need to reset the page, sortBy, and filterBy for the new entity
  reset = () => {
    this.setState(
      {
        page: 1,
        sortBy: { index: 0, direction: SortByDirection.asc },
        filterBy: {}
      },
      () => {
        if (this.toolbarRef) {
          this.toolbarRef.reset();
        }
      }
    );
  };

  // an action was clicked on a row's kebab menu
  handleAction = ({ action, rowData }) => {
    if (action === "UPDATE") {
      this.props.handleEntityAction(action, rowData.data);
    } else {
      this.setState({ action, data: rowData.data });
    }
  };

  cancelledAction = () => {
    this.setState({ action: null });
  };

  // show the confirmation modal for an action
  doAction = () => {
    const props = {
      showNow: true,
      cancelledAction: this.cancelledAction,
      ...this.props
    };
    return this.dataSource.actionButton({
      action: this.state.action,
      props: props,
      click: this.didAction,
      record: this.state.data,
      i: 0,
      asButton: false
    });
  };

  // called by action modal after action is performed or cancelled
  didAction = () => {
    this.setState({ action: null, data: null }, this.update);
  };

  render() {
    const tableProps = {
      cells: this.columns,
      rows: this.state.rows,
      actions: this.dataSource.actionMenuItems(this.props.entity, this.handleAction),
      "aria-label": this.props.entity,
      sortBy: this.state.sortBy,
      onSort: this.onSort,
      variant: TableVariant.compact
    };

    if (this.state.redirect) {
      return (
        <Navigate
          to={{
            pathname: this.dataSource.detailPath || "/details",
            state: this.state.redirectState
          }}
        />
      );
    }

    // map of actions to buttons for the table toolbar
    const actionButtons = () => {
      // don't show UPDATE or DELETE for the entire list of records
      const actions = this.dataSource
        .actions(this.props.entity)
        .filter(action => action !== "UPDATE" && action !== "DELETE");
      const buttons = {};
      actions.forEach((action, i) => {
        buttons[action] = this.dataSource.actionButton({
          action,
          props: this.props,
          click: this.handleAction,
          i,
          asButton: true
        });
      });
      return buttons;
    };

    return (
      <React.Fragment>
        <TableToolbar
          ref={el => (this.toolbarRef = el)}
          total={this.state.total}
          page={this.state.page}
          perPage={this.state.perPage}
          onSetPage={this.onSetPage}
          onPerPageSelect={this.onPerPageSelect}
          fields={this.dataSource.fields}
          filterBy={this.state.filterBy}
          handleChangeFilterValue={this.handleChangeFilterValue}
          hidePagination={true}
          actionButtons={actionButtons()}
        />
        {this.state.rows.length > 0 ? (
          <React.Fragment>
            <Table {...tableProps}>
              <TableHeader />
              <TableBody />
            </Table>
            {this.renderPagination("bottom")}
          </React.Fragment>
        ) : (
          <EmptyTable entity={this.props.entity} />
        )}
        {this.state.action && this.doAction()}
      </React.Fragment>
    );
  }
}

export default EntityListTable;
