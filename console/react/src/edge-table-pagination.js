import React from "react";
import { Pagination } from "@patternfly/react-core";

class EdgeTablePagination extends React.Component {
  constructor(props) {
    super(props);
    this.state = {};
  }

  render() {
    return (
      <Pagination
        itemCount={this.props.rows}
        perPage={this.props.perPage}
        page={this.props.page}
        onSetPage={this.props.onSetPage}
        widgetId="pagination-options-menu-top"
        onPerPageSelect={this.props.onPerPageSelect}
      />
    );
  }
}

export default EdgeTablePagination;
