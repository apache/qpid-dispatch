import React from "react";
import { render, fireEvent } from '@testing-library/react';
import TableToolbar from "./tableToolbar";

it("should render tableToolbar", () => {
  let handleChangeFilterValueCalled = false;
  const props = {
    fields: [{ title: "field0" }, { title: "field1" }],
    filterBy: { value: "f" },
    handleChangeFilterValue: () => handleChangeFilterValueCalled = true,
    total: 2,
    page: 1,
    perPage: 1,
    onSetPage: () => { },
    onPerPageSelect: () => { }
  }
  const { getByLabelText } = render(
    <TableToolbar {...props} />
  );
  expect(getByLabelText("toolbar-pagination")).toBeInTheDocument()
  const filterInput = getByLabelText("search text input");
  expect(filterInput).toBeInTheDocument()

  fireEvent.change(filterInput, { target: { value: 'fi' } });
  expect(handleChangeFilterValueCalled).toBe(true);

  const paginationInput = getByLabelText("Current page");
  expect(paginationInput).toBeInTheDocument();
});
