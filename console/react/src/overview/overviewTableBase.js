import React from "react";
import {
  SortByDirection,
  Table,
  TableHeader,
  TableBody
} from "@patternfly/react-table";
import { Pagination, Title } from "@patternfly/react-core";

import TableToolbar from "./tableToolbar";

class OverviewTableBase extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      sortBy: {},
      filterBy: {},
      perPage: 10,
      total: 1,
      page: 1,
      loading: true,
      columns: [],
      allRows: [],
      rows: [
        {
          cells: ["QDR.A", "0", "interior", "1", "2", "3"]
        },
        {
          cells: [
            {
              title: <div>QDR.B</div>,
              props: { title: "hover title", colSpan: 3 }
            },
            "2",
            "3",
            "4"
          ]
        },
        {
          cells: [
            "QDR.C",
            "0",
            "interior",
            "3",
            {
              title: "four",
              props: { textCenter: false }
            },
            "5"
          ]
        }
      ]
    };
  }

  componentDidMount() {
    this.mounted = true;
    console.log("overviewTable componentDidMount");
    // initialize the columns and get the data
    this.setState({ columns: this.fields }, () => {
      this.update();
      this.timer = setInterval(this.upate, 5000);
    });
  }

  componentWillUnmount = () => {
    this.mounted = false;
    clearInterval(this.timer);
  };

  update = () => {
    this.fetch(this.state.page, this.state.perPage);
  };

  fetch = (page, perPage) => {
    this.setState({ loading: true });
    // doFetch is defined in the derived class
    this.doFetch(page, perPage).then(sliced => {
      // if fetch was called and the component was unmounted before
      // the results arrived, don't call setState
      if (!this.mounted) return;
      const { rows, page, total, allRows } = sliced;
      this.setState({
        rows,
        loading: false,
        page,
        perPage,
        total,
        allRows
      });
    });
  };

  onSort = (_event, index, direction) => {
    const rows = this.sort(this.state.allRows, index, direction);
    this.setState({ rows, page: 1, sortBy: { index, direction } });
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
    console.log(`handleChangeFilterValue(${field}, ${value})`);
  };

  field2Row = field => ({
    cells: this.fields.map(f => field[f.field])
  });

  cellIndex = field => {
    return this.fields.findIndex(f => {
      return f.title === field;
    });
  };
  slice = (fields, page, perPage) => {
    const filterField = this.state.filterBy.field;
    const filterValue = this.state.filterBy.value;
    let rows = fields.map(f => this.field2Row(f));
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
    rows = this.sort(rows);
    const total = rows.length;
    const newPages = Math.ceil(total / perPage);
    page = Math.min(page, newPages);
    const start = perPage * (page - 1);
    const end = Math.min(start + perPage, rows.length);
    const slicedRows = rows.slice(start, end);
    return { rows: slicedRows, page, total, allRows: rows };
  };

  sort = rows => {
    if (
      typeof this.state.index === "undefined" ||
      typeof this.state.direction === "undefined"
    ) {
      return rows;
    }
    rows.sort((a, b) =>
      a.cells[this.sate.index] < b.cells[this.sate.index]
        ? -1
        : a.cells[this.sate.index] > b.cells[this.sate.index]
        ? 1
        : 0
    );
    if (this.sate.direction === SortByDirection.desc) {
      rows = rows.reverse();
    }
    return rows;
  };

  render() {
    console.log("OverviewTable rendered");
    const { loading } = this.state;
    return (
      <React.Fragment>
        <TableToolbar
          total={this.state.total}
          page={this.state.page}
          perPage={this.state.perPage}
          onSetPage={this.onSetPage}
          onPerPageSelect={this.onPerPageSelect}
          fields={this.fields}
          handleChangeFilterValue={this.handleChangeFilterValue}
        />
        {!loading && (
          <Table
            cells={this.state.columns}
            rows={this.state.rows}
            aria-label={this.props.entity}
            sortBy={this.state.sortBy}
            onSort={this.onSort}
          >
            <TableHeader />
            <TableBody />
          </Table>
        )}
        {this.renderPagination("bottom")}
        {loading && (
          <center>
            <Title size="3xl">Please wait while loading data</Title>
          </center>
        )}
      </React.Fragment>
    );
  }
}

export default OverviewTableBase;
