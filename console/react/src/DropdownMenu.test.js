import React from "react";
import { render } from '@testing-library/react';
import DropdownMenu from "./DropdownMenu";

it("the dropdown menu component renders and calls event handlers", () => {
  const isVisible = true;
  const isConnected = () => true;
  let logoutCalled = false;
  const handleDropdownLogout = () => logoutCalled = true
  let menuRef = null;
  render(
    <DropdownMenu
      ref={el => (menuRef = el)}
      isVisible={isVisible} isConnected={isConnected}
      handleDropdownLogout={handleDropdownLogout} />
  );
  menuRef.show(true)
  menuRef.logout()
  expect(logoutCalled).toBe(true)
});
