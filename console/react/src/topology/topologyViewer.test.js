import React from "react";
import { render } from '@testing-library/react';
import TopologyViewer from "./topologyViewer";
import { mockService } from "../qdrService.mock";

it("renders the TopologyViewer component", () => {
  const props = {
    service: mockService({})
  }
  const { getByLabelText } = render(
    <TopologyViewer {...props} />
  )

  // make sure it rendered the component
  const pfTopologyView = getByLabelText("topology-viewer");
  expect(pfTopologyView).toBeInTheDocument();
  expect(getByLabelText("topology-diagram")).toBeInTheDocument();

  // make sure it created the svg
  expect(getByLabelText("topology-svg")).toBeInTheDocument();
});
