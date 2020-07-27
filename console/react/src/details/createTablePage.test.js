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
import CreateTablePage from "./createTablePage";
import { service, login, TEST_PORT } from "../serviceTest";

it("renders a CreateTablePage", () => {
  if (!TEST_PORT) {
    console.log("using mock service");
  }
  login(() => {
    expect(service.management.connection.is_connected()).toBe(true);

    const listenerName = "testListener";
    const props = {
      entity: "listener",
      service,
      schema: service.schema,
      locationState: {},
      routerId: Object.keys(service.management.topology._nodeInfo)[0],
      handleAddNotification: () => {},
      handleActionCancel: () => {}
    };
    const { getByLabelText, getByText } = render(<CreateTablePage {...props} />);

    // the create form should be present
    const createForm = getByLabelText("create-entity-form");
    expect(createForm).toBeInTheDocument();

    // add a listener name
    fireEvent.change(getByLabelText(/name/i), { target: { value: listenerName } });

    // there should be a create button
    const createButton = getByText("Create");
    expect(createButton).toBeInTheDocument();

    // clicking the create button should submit the method
    fireEvent.click(createButton);
  });
});
