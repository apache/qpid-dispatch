import React from "react";
import { render } from '@testing-library/react';
import App from "./App";

const title = "Apache Qpid Dispatch Console"
const config = { title };
it("renders without crashing", () => {
  render(<App config={config} />);
});

it('renders the correct title', () => {
  const { getAllByText } = render(<App config={config} />);
  expect(getAllByText(title));
});