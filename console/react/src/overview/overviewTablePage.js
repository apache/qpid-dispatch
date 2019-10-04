import React from "react";
import { PageSection, PageSectionVariants } from "@patternfly/react-core";
import {
  Stack,
  StackItem,
  TextContent,
  Text,
  TextVariants
} from "@patternfly/react-core";
import { Card, CardBody } from "@patternfly/react-core";

import RoutersTable from "./routersTable";
import AddressesTable from "./addressesTable";
import LinksTable from "./linksTable";
import ConnectionsTable from "./connectionsTable";

class OverviewTablePage extends React.Component {
  constructor(props) {
    super(props);
    this.state = {};
  }

  whichTable = () => {
    if (this.props.entity === "routers") {
      return (
        <RoutersTable entity={this.props.entity} service={this.props.service} />
      );
    }
    if (this.props.entity === "addresses") {
      return (
        <AddressesTable
          entity={this.props.entity}
          service={this.props.service}
        />
      );
    }
    if (this.props.entity === "links") {
      return (
        <LinksTable entity={this.props.entity} service={this.props.service} />
      );
    }
    if (this.props.entity === "connections") {
      return (
        <ConnectionsTable
          entity={this.props.entity}
          service={this.props.service}
        />
      );
    }
  };
  render() {
    return (
      <React.Fragment>
        <PageSection
          variant={PageSectionVariants.light}
          className="overview-table-page"
        >
          <Stack>
            <StackItem className="overview-header">
              <TextContent>
                <Text className="overview-title" component={TextVariants.h1}>
                  {this.props.entity}
                </Text>
              </TextContent>
            </StackItem>
            <StackItem className="overview-table">
              <Card>
                <CardBody>{this.whichTable()}</CardBody>
              </Card>
            </StackItem>
          </Stack>
        </PageSection>
      </React.Fragment>
    );
  }
}

export default OverviewTablePage;
