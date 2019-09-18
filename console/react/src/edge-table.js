import React from "react";
import { Button, ClipboardCopy, TextInput } from "@patternfly/react-core";
import {
  cellWidth,
  Table,
  TableHeader,
  TableBody,
  TableVariant
} from "@patternfly/react-table";
import EdgeTableToolbar from "./edge-table-toolbar";
import EdgeTablePagination from "./edge-table-pagination";
import EmptyEdgeClassTable from "./empty-edge-class-table";
import Graph from "./graph";
import { RouterStates } from "./nodes";

const YAML = 0;
const STATE_ICON = 1;
const STATE_TEXT = 2;
const NAME = 3;

class EdgeTable extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      columns: [
        {
          title: "yaml",
          cellFormatters: [this.formatYaml],
          transforms: [cellWidth(5)]
        },
        {
          title: "State",
          cellFormatters: [this.formatState],
          transforms: [cellWidth(5)]
        },
        { title: "", cellFormatters: [this.formatStateDescription] },
        { title: "Name", cellFormatters: [this.formatName] }
      ],
      filterText: "",
      sortDown: true,
      editingEdgeRow: -1,
      page: 1,
      perPage: 5
    };

    this.rows = [];
  }

  onSelect = (event, isSelected, rowId) => {
    // the internal rows array may be different from the props.rows array
    const realRowIndex =
      rowId >= 0
        ? this.props.rows.findIndex(r => r.key === this.rows[rowId].key)
        : rowId;
    this.props.handleSelectEdgeRow(realRowIndex, isSelected);
  };

  handleEdgeNameBlur = () => {
    this.onSelect("", false, -1);
    this.setState({ editingEdgeRow: -1 });
  };

  handleEdgeNameClick = rowIndex => {
    this.onSelect("", true, rowIndex);
    this.setState({ editingEdgeRow: rowIndex });
  };

  handleEdgeKeyPress = event => {
    if (event.key === "Enter") {
      this.handleEdgeNameBlur();
    }
  };

  formatYaml = (value, _xtraInfo) => {
    const cells = _xtraInfo.rowData.cells;
    let yaml = <div className="state-placeholder"></div>;
    if (cells[0] && cells[1] === 1) {
      yaml = (
        <ClipboardCopy
          className="state-copy"
          onClick={(event, text) => {
            const clipboard = event.currentTarget.parentElement;
            const el = document.createElement("input");
            el.value = JSON.stringify(cells[0]);
            clipboard.appendChild(el);
            el.select();
            document.execCommand("copy");
            clipboard.removeChild(el);
          }}
        />
      );
    }
    return yaml;
  };

  formatStateDescription = (value, _xtraInfo) => {
    return <div className="state-text">{RouterStates[value]}</div>;
  };

  formatState = (value, _xtraInfo) => {
    return (
      <Graph
        id={`State-${_xtraInfo.rowIndex}`}
        thumbNail={true}
        legend={true}
        dimensions={{ width: 30, height: 30 }}
        nodes={[
          {
            key: `edge-key-${_xtraInfo.rowIndex}-${value}`,
            r: 10,
            type: "interior",
            state: value,
            x: 0,
            y: 0
          }
        ]}
        links={[]}
        notifyCurrentRouter={() => {}}
      />
    );
  };
  formatName = (value, _xtraInfo) => {
    const realRowIndex = this.props.rows.findIndex(
      r => r.key === _xtraInfo.rowData.key
    );
    if (this.state.editingEdgeRow === _xtraInfo.rowIndex) {
      // the internal rows array may be different from the props.rows array
      return (
        <TextInput
          value={this.props.rows[realRowIndex].name}
          type="text"
          autoFocus
          onChange={val => this.props.handleEdgeNameChange(val, realRowIndex)}
          onBlur={this.handleEdgeNameBlur}
          onKeyPress={this.handleEdgeKeyPress}
          aria-label="text input example"
        />
      );
    }
    return (
      <Button
        variant="link"
        isInline
        onClick={() => this.handleEdgeNameClick(_xtraInfo.rowIndex)}
      >
        {this.rows[_xtraInfo.rowIndex].cells[NAME]}
      </Button>
    );
  };

  onSelect = (event, isSelected, rowId) => {
    // the internal rows array may be different from the props.rows array
    const realRowIndex =
      rowId >= 0
        ? this.props.rows.findIndex(r => r.key === this.rows[rowId].key)
        : rowId;
    this.props.handleSelectEdgeRow(realRowIndex, isSelected);
  };

  toggleAlphaSort = () => {
    this.setState({ sortDown: !this.state.sortDown });
  };

  genTable = () => {
    const { columns, filterText } = this.state;
    if (this.props.rows.length > 0) {
      if (this.state.editingEdgeRow === -1 || this.rows.length === 0) {
        this.rows = this.props.rows.map(r => ({
          cells: [r.yaml, r.state, r.state, r.name],
          selected: r.selected,
          key: r.key
        }));
        // sort the rows
        this.rows = this.rows.sort((a, b) =>
          a.cells[NAME] < b.cells[NAME]
            ? -1
            : a.cells[NAME] > b.cells[NAME]
            ? 1
            : 0
        );
        if (!this.state.sortDown) {
          this.rows = this.rows.reverse();
        }
        // filter the rows
        if (filterText !== "") {
          this.rows = this.rows.filter(
            r => r.cells[NAME].indexOf(filterText) >= 0
          );
        }
        // only show rows on current page
        const start = (this.state.page - 1) * this.state.perPage;
        const end = Math.min(this.rows.length, start + this.state.perPage);
        this.rows = this.rows.slice(start, end);
      } else {
        // pickup any changed info
        this.rows.forEach(r => {
          const rrow = this.props.rows.find(rr => rr.key === r.key);
          if (rrow) {
            r.selected = rrow.selected;
            r.cells[YAML] = rrow.yaml;
            r.cells[STATE_ICON] = rrow.state;
            r.cells[STATE_TEXT] = rrow.state;
            r.cells[NAME] = rrow.name;
          }
        });
      }

      return (
        <React.Fragment>
          <Table
            className="edge-table"
            variant={TableVariant.compact}
            onSelect={this.onSelect}
            cells={columns}
            rows={this.rows}
          >
            <TableHeader />
            <TableBody />
          </Table>
        </React.Fragment>
      );
    }
    return <EmptyEdgeClassTable handleAddEdge={this.props.handleAddEdge} />;
  };

  handleChangeFilter = filterText => {
    let { page } = this.state;
    if (filterText !== "") {
      page = 1;
    }
    this.setState({ filterText, page });
  };

  genToolbar = () => {
    if (this.props.rows.length > 0) {
      return (
        <React.Fragment>
          <label>Edge namespaces</label>
          <EdgeTableToolbar
            handleAddEdge={this.props.handleAddEdge}
            handleDeleteEdge={this.props.handleDeleteEdge}
            handleChangeFilter={this.handleChangeFilter}
            toggleAlphaSort={this.toggleAlphaSort}
            filterText={this.state.filterText}
            sortDown={this.state.sortDown}
            rows={this.props.rows}
          />
        </React.Fragment>
      );
    }
  };

  onSetPage = (_event, pageNumber) => {
    this.setState({
      page: pageNumber
    });
  };

  onPerPageSelect = (_event, perPage) => {
    this.setState({
      perPage
    });
  };

  genPagination = () => {
    if (this.props.rows.length > 0) {
      return (
        <EdgeTablePagination
          rows={this.props.rows.length}
          perPage={this.state.perPage}
          page={this.state.page}
          onPerPageSelect={this.onPerPageSelect}
          onSetPage={this.onSetPage}
        />
      );
    }
  };
  render() {
    return (
      <React.Fragment>
        {this.genToolbar()}
        {this.genTable()}
        {this.genPagination()}
      </React.Fragment>
    );
  }
}

export default EdgeTable;
