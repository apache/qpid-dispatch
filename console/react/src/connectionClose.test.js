import React from "react";
import { render, fireEvent } from '@testing-library/react';
import ConnectionClose from "./connectionClose";
import { mockService } from "./qdrService.mock";

it("renders the ConnectionClose component", () => {
  let sendMethodCalled = false;
  let handleAddNotificationCalled = false;
  let notifyClickCalled = false;

  const props = {
    service: mockService({ onSendMethod: () => sendMethodCalled = true }),
    handleAddNotification: () => handleAddNotificationCalled = true,
    notifyClick: () => notifyClickCalled = true,
    extraInfo: { rowData: { data: { name: "test record", role: "normal" } } }
  }
  const { getByLabelText, queryByLabelText } = render(
    <ConnectionClose {...props} />
  )

  // the close button should be there
  const closeButton = getByLabelText("connection-close-button");
  expect(closeButton).toBeInTheDocument();

  // the confirmation dialog should not be there
  expect(queryByLabelText("connection-close-modal")).toBeNull();

  // clicking the close button should display the confirmation dialog
  fireEvent.click(closeButton);
  expect(getByLabelText("connection-close-modal")).toBeInTheDocument();

  const confirmButton = getByLabelText("connection-close-confirm");
  expect(confirmButton).toBeInTheDocument();
  fireEvent.click(confirmButton);

  expect(sendMethodCalled).toBe(true);
  setTimeout(() => {
    expect(handleAddNotificationCalled).toBe(true);
    expect(notifyClickCalled).toBe(true);
  }, 1);

});
