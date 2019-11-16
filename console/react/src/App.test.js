import React from "react";
import { render } from "@testing-library/react";
import { axe, toHaveNoViolations } from "jest-axe";

import App from "./App";

expect.extend(toHaveNoViolations);

const title = "Apache Qpid Dispatch Console";
const config = { title };
it("renders the correct title without accessibility violations", async () => {
  const { container, getAllByText } = render(<App config={config} />);

  const results = await axe(container);
  expect(results).toHaveNoViolations();

  expect(getAllByText(title));
});
