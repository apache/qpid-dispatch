import React from "react";
import { render, fireEvent, waitForElement } from "@testing-library/react";
import { mockService } from "../qdrService.mock";
import ChordViewer from "./chordViewer";
import { LocalStorageMock } from "@react-mock/localstorage";

it("renders the ChordViewer component", async () => {
  const props = {
    service: mockService({})
  };
  const { getByLabelText } = render(
    <LocalStorageMock
      items={{
        chordOptions: JSON.stringify({
          isRate: false,
          byAddress: true
        })
      }}
    >
      <ChordViewer {...props} />
    </LocalStorageMock>
  );

  // make sure it rendered the component
  const pfTopologyView = getByLabelText("chord-viewer");
  expect(pfTopologyView).toBeInTheDocument();

  // make sure it created the svg
  expect(getByLabelText("chord-svg")).toBeInTheDocument();

  const optionsButton = getByLabelText("button-for-Options");
  fireEvent.click(optionsButton);

  // turn on show by address
  const addressButton = getByLabelText("show by address");
  fireEvent.click(addressButton);

  // after 1 update period, the B chord should be in the svg
  await waitForElement(() => getByLabelText("B"));
});
