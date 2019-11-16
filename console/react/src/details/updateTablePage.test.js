import React from "react";
import { render, fireEvent } from '@testing-library/react';
import { mockService } from "../qdrService.mock";
import UpdateTablePage from "./updateTablePage";

it('renders a UpdateTablePage', () => {
  let sendMethodCalled = false;
  const service = mockService({ onSendMethod: () => sendMethodCalled = true });
  const props = {
    entity: "log",
    service,
    schema: service.schema,
    locationState: { currentRecord: { name: "test.log.name", enable: "" } },
    routerId: Object.keys(service.management.topology._nodeInfo)[0],
    handleAddNotification: () => { },
    handleActionCancel: () => { }
  }
  const {
    getByLabelText,
    getByText
  } = render(<UpdateTablePage {...props} />);

  // the update form should be present
  expect(getByLabelText("update-entity-form")).toBeInTheDocument();

  // there should be a create button
  const updateButton = getByText("Update");
  expect(updateButton).toBeInTheDocument();

  // clicking the create button should do nothing
  // since it is disabled until a form field changes
  fireEvent.click(updateButton);
  expect(sendMethodCalled).toBe(false);

  // change a form field and try the update button again
  fireEvent.change(getByLabelText(/enable/i), { target: { value: 'debug' } });
  fireEvent.click(updateButton);
  expect(sendMethodCalled).toBe(true);

})