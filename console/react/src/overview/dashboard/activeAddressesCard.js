import React from "react";
import { Table, TableHeader, TableBody } from "@patternfly/react-table";

// update the table every 5 seconds
const UPDATE_INTERVAL = 1000 * 5;

class ActiveAddressesCard extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      lastUpdate: new Date(),
      columns: ["Address", "Class", "Settle rate"],
      rows: []
    };
  }

  componentDidMount = () => {
    this.timer = setInterval(this.updateData, UPDATE_INTERVAL);
    this.updateData();
  };

  componentWillUnmount = () => {
    clearInterval(this.timer);
  };

  updateData = () => {
    this.props.service.management.topology.fetchAllEntities(
      {
        entity: "router.link",
        attrs: ["settleRate", "linkType", "linkDir", "owningAddr"]
      },
      results => {
        let active = {};
        for (let id in results) {
          const aresult = results[id]["router.link"];
          for (let i = 0; i < aresult.results.length; i++) {
            const result = this.props.service.utilities.flatten(
              aresult.attributeNames,
              aresult.results[i]
            );
            if (result.linkType === "endpoint" && result.linkDir === "out") {
              if (
                parseInt(result.settleRate) > 0 &&
                !result.owningAddr.startsWith("Ltemp.")
              ) {
                if (!active.hasOwnProperty[result.owningAddr]) {
                  active[result.owningAddr] = {
                    addr: this.props.service.utilities.addr_text(
                      result.owningAddr
                    ),
                    cls: this.props.service.utilities.addr_class(
                      result.owningAddr
                    ),
                    settleRate: 0
                  };
                }
                active[result.owningAddr].settleRate += parseInt(
                  result.settleRate
                );
              }
            }
          }
        }
        let rows = Object.keys(active).map(addr => {
          return {
            cells: [
              active[addr].addr,
              active[addr].cls,
              active[addr].settleRate.toLocaleString()
            ]
          };
        });
        this.setState({ rows, lastUpdate: new Date() });
      }
    );
  };

  nextUpdateString = () => {
    const nextUpdate = new Date(
      this.state.lastUpdate.getTime() + UPDATE_INTERVAL
    );
    return this.props.service.utilities.strDate(nextUpdate);
  };

  lastUpdateString = () => {
    return this.props.service.utilities.strDate(this.state.lastUpdate);
  };
  render() {
    const { columns, rows } = this.state;

    const caption = (
      <React.Fragment>
        <span className="caption">Most active addresses</span>
        <div className="updated">
          Updated at {this.lastUpdateString()} | Next {this.nextUpdateString()}
        </div>
      </React.Fragment>
    );
    return (
      <Table caption={caption} cells={columns} rows={rows}>
        <TableHeader />
        <TableBody />
      </Table>
    );
  }
}

export default ActiveAddressesCard;
