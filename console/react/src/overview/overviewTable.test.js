import React from "react";
import { render } from '@testing-library/react';
import OverviewTable from "./overviewTable";
import { mockService } from "../qdrService.mock";

it("renders the OverviewTable component", () => {
  const entity = "routers";
  const props = {
    service: mockService({}),
    location: { pathname: `/overview/${entity}` },
    lastUpdated: () => { }
  }
  const { getByLabelText } = render(
    <OverviewTable {...props} />
  )

  // make sure it rendered the component
  expect(getByLabelText(entity)).toBeInTheDocument();
});
