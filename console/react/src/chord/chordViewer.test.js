import React from "react";
import { render } from '@testing-library/react';
import ChordViewer from "./chordViewer";
import { mockService } from "../qdrService.mock";

it("renders the TopologyViewer component", () => {
  const props = {
    service: mockService({})
  }
  const { getByLabelText } = render(
    <ChordViewer {...props} />
  )

  // make sure it rendered the component
  const pfTopologyView = getByLabelText("chord-viewer");
  expect(pfTopologyView).toBeInTheDocument();

  // make sure it created the svg. Note: this will be the empty circle
  // since there is no traffic
  expect(getByLabelText("chord-svg")).toBeInTheDocument();
});
