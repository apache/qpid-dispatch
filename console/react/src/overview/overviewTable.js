import React from "react";
import { Table, TableHeader, TableBody } from "@patternfly/react-table";

import { Pagination, Title } from "@patternfly/react-core";
import TableToolbar from "./tableToolbar";

class OverviewTable extends React.Component {
  constructor(props, helper) {
    super(props);
    this.state = {
      sortBy: {},
      perPage: 10,
      total: 1,
      page: 1,
      loading: true,
      columns: helper.fields,
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

  fetch = (page, perPage) => {
    this.setState({ loading: true });
    this.helper.fetch(perPage, page, this.state.sortBy).then(sliced => {
      const { rows, page, total, allRows } = sliced;
      this.setState({
        rows,
        loading: false,
        perPage,
        page,
        total,
        allRows
      });
    });
  };

  onSort = (_event, index, direction) => {
    const rows = this.helper.sort(this.state.allRows, index, direction);
    this.setState({ rows, page: 1, sortBy: { index, direction } });
  };

  componentDidMount() {
    console.log("overviewTable componentDidMount");
    this.fetch(this.state.page, this.state.perPage);
  }

  renderPagination(variant = "top") {
    const { page, perPage, total } = this.state;
    return (
      <Pagination
        itemCount={total}
        page={page}
        perPage={perPage}
        onSetPage={(_evt, value) => this.fetch(value, perPage)}
        onPerPageSelect={(_evt, value) => this.fetch(1, value)}
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

export default OverviewTable;
