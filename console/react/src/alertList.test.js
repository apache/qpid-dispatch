import React from "react";
import { render } from '@testing-library/react';
import AlertList from "./alertList";

it("renders the AlertList component", () => {
  let ref = null;
  const props = {
  }
  const { getByLabelText, queryByLabelText } = render(
    <AlertList
      ref={el => (ref = el)}
      {...props} />)
  // the container should be there
  expect(getByLabelText("alert-list")).toBeInTheDocument();
  // there should be no alerts in the list to start with
  expect(queryByLabelText("alert-close-button")).toBeNull();

  // add an alert
  ref.addAlert("info", "testing");
  // the alert close button should now be there
  expect(getByLabelText("alert-close-button")).toBeInTheDocument();

  const alert = {
    key: 0
  }
  // hide the alert
  ref.hideAlert(alert);
  // the alert close button should now be gone
  expect(queryByLabelText("alert-close-button")).toBeNull();
});
