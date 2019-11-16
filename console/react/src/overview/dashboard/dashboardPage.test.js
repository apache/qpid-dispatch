import React from "react";
import { render } from '@testing-library/react';
import DashboardPage from "./dashboardPage";
import { mockService } from "../../qdrService.mock";

it("renders the DashboardPage component", () => {
  const props = {
    service: mockService({})
  }
  const { getByLabelText } = render(
    <DashboardPage {...props} />
  )

  // make sure it rendered the component
  expect(getByLabelText("dashboard-page")).toBeInTheDocument();
});
