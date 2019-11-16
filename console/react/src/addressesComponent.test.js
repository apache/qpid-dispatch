import React from "react";
import { render, fireEvent } from '@testing-library/react';
import AddressesComponent from "./addressesComponent";

it("renders the addresses component with an address", () => {
  let handleChangeAddressCalled = false;
  let handleHoverAddress = undefined;
  const props = {
    addresses: { test: true },
    addressColors: { test: "#EAEAEA" },
    handleChangeAddress: () => handleChangeAddressCalled = true,
    handleHoverAddress: (address, over) => handleHoverAddress = over
  }
  const { getByLabelText } = render(
    <AddressesComponent
      {...props} />);
  const node = getByLabelText("colored dot");
  fireEvent.click(node)
  expect(handleChangeAddressCalled).toBe(true)
  fireEvent.mouseOver(node);
  expect(handleHoverAddress).toBe(true)
  fireEvent.mouseOut(node);
  expect(handleHoverAddress).toBe(false)
});

it("renders the addresses component without an address", () => {
  const props = {
    addresses: {},
    addressColors: {},
  }
  const { getByText } = render(
    <AddressesComponent
      {...props} />);
  expect(getByText("There is no traffic")).toBeInTheDocument();
});

