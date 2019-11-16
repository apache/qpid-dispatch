import React from "react";
import { render } from '@testing-library/react';
import DetailsTablePage from "./detailsTablePage";

it("renders the detailsTablePage", () => {
  const entity = "testEntity"
  const props = {
    entity,
    locationState: { currentRecord: { name: "test" } },
    details: true,
    schema: { entityTypes: { testEntity: { attributes: [], operations: [] } } },
    service: { management: { topology: { fetchEntities: () => Promise.resolve([]) } } }
  }
  const { getByLabelText } = render(
    <DetailsTablePage {...props} />
  );
  const table = getByLabelText(entity);
  expect(table).toBeInTheDocument();
});
