import React from "react";
import { PageSection, PageSectionVariants } from "@patternfly/react-core";
import { Stack, StackItem } from "@patternfly/react-core";
import { Card, CardHeader, CardBody } from "@patternfly/react-core";
import { Split, SplitItem } from "@patternfly/react-core";
import ThroughputCard from "./throughputCard";
import InFlightCard from "./inFlightCard";
import ActiveAddressesCard from "./activeAddressesCard";

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
          className="overview-charts-page"
        >
          <Stack gutter="md">
            <StackItem>
              <Card>
                <CardHeader>Header</CardHeader>
                <CardBody>
                  <ThroughputCard />
                  <InFlightCard />
                </CardBody>
              </Card>
            </StackItem>
            <StackItem>
              <Split gutter="md">
                <SplitItem>
                  <ActiveAddressesCard />
                </SplitItem>
                <SplitItem className="fill-card">
                  <ActiveAddressesCard />
                </SplitItem>
              </Split>
            </StackItem>
          </Stack>
        </PageSection>
      </React.Fragment>
    );
  }
}

export default OverviewChartsPage;
