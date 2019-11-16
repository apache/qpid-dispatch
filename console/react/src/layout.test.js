import React from "react";
import { render, fireEvent } from '@testing-library/react';
import PageLayout from "./layout";
import { mockService } from "./qdrService.mock";

it('allows the user to login successfully', async () => {
  const title = "Test Layout Page";
  const props = {
    config: { title: title },
    service: mockService({})
  };

  const {
    getAllByText,
    getByLabelText,
    findByLabelText,
    getByTestId } = render(<PageLayout {...props} />);

  // the correct title should be found
  expect(getAllByText(title));

  // fill out the form
  fireEvent.change(getByLabelText(/address/i), { target: { value: 'localhost' } });
  fireEvent.change(getByLabelText(/port/i), { target: { value: '5673' } });

  fireEvent.click(getByTestId("connect-button"));

  // wait for the dashboard
  // to show up before continuing with our assertions.
  const dashboard = await findByLabelText('dashboard-page');

  expect(dashboard).toHaveTextContent(/Router network statistics/i);
})