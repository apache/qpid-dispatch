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
import { mockService } from "../../test_data/qdrService.mock";
import UpdateTablePage from "./updateTablePage";

it("renders a UpdateTablePage", () => {
  let sendMethodCalled = false;
  const service = mockService({ onSendMethod: () => (sendMethodCalled = true) });
  const props = {
    entity: "log",
    service,
    schema: service.schema,
    locationState: { currentRecord: { name: "test.log.name", enable: "" } },
    routerId: Object.keys(service.management.topology._nodeInfo)[0],
    handleAddNotification: () => {},
    handleActionCancel: () => {}
  };
  const { getByLabelText, getByText } = render(<UpdateTablePage {...props} />);

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
  fireEvent.change(getByLabelText(/enable/i), { target: { value: "debug" } });
  fireEvent.click(updateButton);
  expect(sendMethodCalled).toBe(true);
});
