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
