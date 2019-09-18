import React from "react";
import {
  Title,
  EmptyState,
  EmptyStateVariant,
  EmptyStateIcon,
  EmptyStateBody
} from "@patternfly/react-core";
import { CubesIcon } from "@patternfly/react-icons";

class EmptySelection extends React.Component {
  constructor(props) {
    super(props);
    this.state = {};
  }

  render() {
    return (
      <EmptyState variant={EmptyStateVariant.small} className="empty-selection">
        <EmptyStateIcon icon={CubesIcon} />
        <Title headingLevel="h5" size="lg">
          Nothing selected
        </Title>
        <EmptyStateBody>
          Select a cluster, edge class, or line between them to view/edit their
          information.
        </EmptyStateBody>
      </EmptyState>
    );
  }
}

export default EmptySelection;
