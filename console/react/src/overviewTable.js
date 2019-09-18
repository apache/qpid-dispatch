import React from "react";
import {
  Table,
  TableHeader,
  TableBody,
  textCenter
} from "@patternfly/react-table";

import { Pagination, Title } from "@patternfly/react-core";

class OverviewTable extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      res: [],
      perPage: 20,
      total: 100,
      page: 1,
      error: null,
      loading: true,
      columns: [
        { title: "Router" },
        "Area",
        { title: "Mode" },
        "Addresses",
        {
          title: "Links",
          transforms: [textCenter],
          cellTransforms: [textCenter]
        },
        {
          title: "External connections",
          transforms: [textCenter],
          cellTransforms: [textCenter]
        }
      ],
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

  fetch(page, perPage) {
    this.setState({ loading: true });
    fetch(
      `https://jsonplaceholder.typicode.com/posts?_page=${page}&_limit=${perPage}`
    )
      .then(resp => resp.json())
      .then(resp => this.setState({ res: resp, perPage, page, loading: false }))
      .catch(err => this.setState({ error: err, loading: false }));
  }

  componentDidMount() {
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

  render() {
    const { loading } = this.state;
    return (
      <React.Fragment>
        {this.renderPagination()}
        {!loading && (
          <Table cells={this.state.columns} rows={this.state.rows}>
            <TableHeader />
            <TableBody />
          </Table>
        )}
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
