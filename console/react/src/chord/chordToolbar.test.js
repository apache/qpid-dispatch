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
import ChordToolbar from "./chordToolbar";

it("renders a ChordToolbar", () => {
  const routerName = "testRouterName";
  const props = {
    isRate: true,
    byAddress: true,
    handleAddNotification: () => {},
    handleChangeOption: () => {},
    addresses: { testAddress: true },
    chordColors: { testAddress: "#EAEAEA" },
    arcColors: { testRouterName: "#EBAEBA" },
    handleChangeAddress: () => {},
    handleHoverAddress: () => {},
    handleHoverRouter: () => {}
  };
  const { getByLabelText, getByText, getByTestId } = render(<ChordToolbar {...props} />);

  // the toolbar should be present
  expect(getByTestId("chord-toolbar")).toBeInTheDocument();

  // the options drowdown button should be present
  const optionsButton = getByLabelText("button-for-Options");
  expect(optionsButton).toBeInTheDocument();

  fireEvent.click(optionsButton);
  // clicking on the options buttons should show the address panel
  const showByAddressCheckbox = getByLabelText("show by address");
  expect(showByAddressCheckbox).toBeInTheDocument();

  expect(getByText("testAddress")).toBeInTheDocument();

  // the options drowdown button should be present
  const routersButton = getByLabelText("button-for-Routers");
  expect(routersButton).toBeInTheDocument();

  fireEvent.click(routersButton);
  // clicking on the Routers buttons should show the list of routers
  expect(getByText(routerName)).toBeInTheDocument();
});
