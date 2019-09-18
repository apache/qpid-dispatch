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
import OverviewTable from "./overviewTable";

class OverviewChartsPage extends React.Component {
  constructor(props) {
    super(props);
    this.state = {};
  }

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
                <CardBody>
                  <OverviewTable
                    tableInfo={this.props.tableInfo}
                    entity={this.props.entity}
                  />
                </CardBody>
              </Card>
            </StackItem>
          </Stack>
        </PageSection>
      </React.Fragment>
    );
  }
}

export default OverviewChartsPage;
