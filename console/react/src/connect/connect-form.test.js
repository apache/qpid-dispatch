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
import ConnectForm from "./connect-form";
import { service, TEST_PORT } from "../serviceTest";

it("renders the connect form", () => {
  render(<ConnectForm />);
});

it("connect form can be submitted", async () => {
  const props = {
    service,
    handleConnect: (section, message, date, severity) => {},
    handleConnectCancel: () => {},
    handleAddNotification: () => {},
    fromPath: "/test",
    prefix: "test",
    isConnectFormOpen: true,
    isConnected: false,
    connecting: false
  };

  const { getByLabelText, getByTestId } = render(<ConnectForm {...props} />);

  // fill out the form
  fireEvent.change(getByLabelText(/address/i), { target: { value: "localhost" } });
  fireEvent.change(getByLabelText(/port/i), {
    target: { value: TEST_PORT || 5673 }
  });

  fireEvent.click(getByTestId("connect-button"));
});
