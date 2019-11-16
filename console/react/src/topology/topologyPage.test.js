import React from "react";
import { render } from "@testing-library/react";
import { mockService } from "../qdrService.mock";
import TopologyPage from "./topologyPage";

it("renders the TopologyPage component", () => {
  const props = {
    service: mockService({})
  };
  const { getByTestId } = render(<TopologyPage {...props} />);

  // make sure it rendered the component
  expect(getByTestId("topology-page")).toBeInTheDocument();
});
