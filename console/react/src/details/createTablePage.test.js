import React from "react";
import { render, fireEvent } from '@testing-library/react';
import CreateTablePage from "./createTablePage";
import { mockService } from "../qdrService.mock";

it('renders a CreateTablePage', () => {
  let sendMethodCalled = false;
  const service = mockService({ onSendMethod: () => sendMethodCalled = true });
  const props = {
    entity: "listener",
    service,
    schema: service.schema,
    locationState: {},
    routerId: Object.keys(service.management.topology._nodeInfo)[0],
    handleAddNotification: () => { },
    handleActionCancel: () => { }
  }
  const {
    getByLabelText,
    getByText
  } = render(<CreateTablePage {...props} />);

  // the create form should be present
  const notificationIcon = getByLabelText("create-entity-form");
  expect(notificationIcon).toBeInTheDocument();

  // there should be a create button
  const createButton = getByText("Create");
  expect(createButton).toBeInTheDocument();

  // clicking the create button should submit the method
  fireEvent.click(createButton);
  expect(sendMethodCalled).toBe(true);
})