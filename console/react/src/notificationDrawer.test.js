import React from "react";
import { render, fireEvent } from '@testing-library/react';
import NotificationDrawer from "./notificationDrawer";

it('renders without crashing', () => {
  render(<NotificationDrawer />);
})

it('renders a notification icon', () => {
  let notificationRef = null;
  const {
    getByLabelText,
    getByText
  } = render(<NotificationDrawer
    ref={el => (notificationRef = el)}
  />);

  // the notifications icon should be present
  const notificationIcon = getByLabelText("Notifications");
  expect(notificationIcon).toBeInTheDocument();

  // add a notification
  const section = "action";
  const message = "test message";
  const timestamp = new Date();
  const severity = "info";
  notificationRef.addNotification({ section, message, timestamp, severity })

  // click the notification icon
  fireEvent.click(notificationIcon);

  // there should now be a single notification-item
  expect(getByText("1 new event")).toBeInTheDocument();
})