/*
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
*/

import React from "react";
import { render, fireEvent } from "@testing-library/react";
import ConnectionClose from "./connectionClose";
import { mockService } from "../../test_data/qdrService.mock";

it("renders the ConnectionClose component", () => {
  let sendMethodCalled = false;
  let handleAddNotificationCalled = false;
  let notifyClickCalled = false;

  const props = {
    service: mockService({ onSendMethod: () => (sendMethodCalled = true) }),
    handleAddNotification: () => (handleAddNotificationCalled = true),
    notifyClick: () => (notifyClickCalled = true),
    extraInfo: { rowData: { data: { name: "test record", role: "normal" } } }
  };
  const { getByLabelText, queryByLabelText } = render(<ConnectionClose {...props} />);

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
