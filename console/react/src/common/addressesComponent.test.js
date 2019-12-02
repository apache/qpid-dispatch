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
import AddressesComponent from "./addressesComponent";

it("renders the addresses component with an address", () => {
  let handleChangeAddressCalled = false;
  let handleHoverAddress = undefined;
  const props = {
    addresses: { test: true },
    addressColors: { test: "#EAEAEA" },
    handleChangeAddress: () => (handleChangeAddressCalled = true),
    handleHoverAddress: (address, over) => (handleHoverAddress = over)
  };
  const { getByLabelText } = render(<AddressesComponent {...props} />);
  const node = getByLabelText("colored dot");
  fireEvent.click(node);
  expect(handleChangeAddressCalled).toBe(true);
  fireEvent.mouseOver(node);
  expect(handleHoverAddress).toBe(true);
  fireEvent.mouseOut(node);
  expect(handleHoverAddress).toBe(false);
});

it("renders the addresses component without an address", () => {
  const props = {
    addresses: {},
    addressColors: {},
    handleChangeAddress: () => {},
    handleHoverAddress: () => {}
  };
  const { getByText } = render(<AddressesComponent {...props} />);
  expect(getByText("There is no traffic")).toBeInTheDocument();
});
