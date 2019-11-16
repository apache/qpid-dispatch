import React from "react";
import { render, fireEvent } from '@testing-library/react';
import ContextMenu from "./contextMenuComponent"

it("the contextMenu component renders and calls event handlers", () => {
  let handleContextHideClicked = false;
  let itemActionCalled = false;
  const props = {
    handleContextHide: () => handleContextHideClicked = true,
    menuItems: [{ enabled: () => true, action: () => itemActionCalled = true }],
    contextEventData: {},
    contextEventPosition: [-1, -1]
  }
  const { getByLabelText } = render(
    <ContextMenu {...props} />
  );
  const menuItem = getByLabelText("context-menu-item");
  expect(menuItem).toBeInTheDocument();

  fireEvent.click(menuItem);
  expect(handleContextHideClicked).toBe(true);
  expect(itemActionCalled).toBe(true);
});
