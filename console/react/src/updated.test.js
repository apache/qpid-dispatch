import React from "react";
import { render } from '@testing-library/react';
import { QDRService } from "./qdrService";
import Updated from "./updated";

it("should render the Updated component", () => {
  const service = new QDRService(() => { });
  const props = {
    lastUpdated: new Date(),
    service
  }
  const { getByLabelText } = render(
    <Updated {...props} />
  );
  expect(getByLabelText("last-updated")).toBeInTheDocument()
});
