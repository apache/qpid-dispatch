import React from "react";
import { Table, TableHeader, TableBody } from "@patternfly/react-table";

class DetailsTable extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      columns: [
        "Link type",
        "Addr",
        "Settle rate",
        "Delayed1",
        "Delayed10",
        "Usage"
      ]
    };
  }

  render() {
    const { columns } = this.state;
    let subRows = this.props.subRows || [];
    return (
      <Table cells={columns} rows={subRows}>
        <TableHeader />
        <TableBody />
      </Table>
    );
  }
}

export default DetailsTable;
