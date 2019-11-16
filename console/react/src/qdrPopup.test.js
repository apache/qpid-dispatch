import React from "react";
import { render } from '@testing-library/react';
import QDRPopup from "./qdrPopup";

it("the popup component renders HTML", () => {
  const text = "Hello world";
  const props = {
    content: `<h1>${text}</h1>`
  }
  const { getByText, getByLabelText } = render(
    <QDRPopup {...props} />
  );
  expect(getByLabelText("popup")).toBeInTheDocument()
  expect(getByText("Hello world")).toBeInTheDocument();
});
