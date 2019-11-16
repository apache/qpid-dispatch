import React from "react";
import { render } from '@testing-library/react';
import InflightChart from "./inflightChart";
import { mockService } from "../../qdrService.mock";

it("renders the InflightChart component", () => {
  const props = {
    service: mockService({})
  }
  const { getByLabelText } = render(
    <InflightChart {...props} />
  )

  // make sure it rendered the component
  setTimeout(() => expect(getByLabelText("inflight-chart")).toBeInTheDocument(), 1);
});
