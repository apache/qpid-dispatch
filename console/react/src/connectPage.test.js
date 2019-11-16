import React from "react";
import { render } from '@testing-library/react';
import ConnectPage from "./connectPage";

it("renders the connect page", () => {
  const locationState = { location: { state: { pathname: "/fromTest", from: "/test" } } }
  const config = { title: "Qpid Dispatch Router Test Console" }
  const { getByText } = render(
    <ConnectPage config={config} location={locationState} />
  );
  expect(getByText(config.title)).toBeInTheDocument();
});
