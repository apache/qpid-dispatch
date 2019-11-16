import React from "react";
import { render } from '@testing-library/react';
import ThroughputChart from "./throughputChart";
import { mockService } from "../../qdrService.mock";

it("renders the ThroughputChart component", () => {
  const props = {
    service: mockService({})
  }
  const { getByLabelText, queryByLabelText } = render(
    <ThroughputChart {...props} />
  )

  // the component should initially render
  // blank until it gets the contianer's width.
  expect(queryByLabelText("throughput-chart")).toBe(null);

  // yeild, so that this componentDidMount method can fire
  setTimeout(() => expect(getByLabelText("throughput-chart")).toBeInTheDocument(), 1);
});
