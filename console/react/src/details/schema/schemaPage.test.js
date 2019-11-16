import React from "react";
import { render, fireEvent } from "@testing-library/react";
import { mockService } from "../../qdrService.mock";
import SchemaPage from "./schemaPage";

it("renders a SchemaPage", () => {
  const service = mockService({});
  const props = {
    schema: service.schema
  };
  const { getByTestId, queryByTestId } = render(<SchemaPage {...props} />);

  // the root node should be present
  const root = getByTestId("entities");
  expect(root).toBeInTheDocument();

  // the root node should be expanded by default
  // therefore the address entity should be present
  let addressEntity = getByTestId("address");
  expect(addressEntity).toBeInTheDocument();

  fireEvent.click(addressEntity);
  expect(getByTestId("address-egressPhase")).toBeInTheDocument();

  // clicking on the root should collapse the tree
  fireEvent.click(root);
  addressEntity = queryByTestId("address");
  expect(addressEntity).toBeNull();
});
