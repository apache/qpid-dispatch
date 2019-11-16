import React from "react";
import { render } from '@testing-library/react';
import { mockService } from "../qdrService.mock";
import OverviewPage from "./overviewPage";

it("renders the overview page", () => {
  const entity = "logs";
  const props = {
    service: mockService({}),
    location: { pathname: `/overview/${entity}` },
    lastUpdated: () => { }
  }

  const { getByTestId } = render(
    <OverviewPage {...props} />
  );
  expect(getByTestId("overview-page")).toBeInTheDocument();
});
