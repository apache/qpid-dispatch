import React from "react";
import { render, fireEvent, waitForElement } from "@testing-library/react";
import { mockService } from "../qdrService.mock";
import TopologyViewer from "./topologyViewer";
import * as world from "../../public/data/countries.json";

it("renders the TopologyViewer component", async () => {
  jest.spyOn(window, "fetch").mockImplementationOnce(() => {
    return Promise.resolve({
      json: () => Promise.resolve(world)
    });
  });
  const props = {
    service: mockService({})
  };
  const { getByLabelText, getByText, getByTestId } = render(
    <TopologyViewer {...props} />
  );

  // make sure it rendered the component
  const pfTopologyView = getByLabelText("topology-viewer");
  expect(pfTopologyView).toBeInTheDocument();
  expect(getByLabelText("topology-diagram")).toBeInTheDocument();

  // make sure it created the svg
  expect(getByLabelText("topology-svg")).toBeInTheDocument();

  // the svg should have a router circle
  await waitForElement(() => getByTestId("router-0"));

  // "click" on the router
  const router = getByTestId("router-0");
  fireEvent.mouseDown(router);
  fireEvent.mouseUp(router);

  // the routerInfo modal appears
  await waitForElement(() => getByLabelText("Close"));
  // close the modal
  fireEvent.click(getByLabelText("Close"));

  // the svg should have a client circle
  const client = getByTestId("client-2");
  // "click" on the client
  fireEvent.mouseDown(client);
  fireEvent.mouseUp(client);

  // the clientInfo modal appears
  await waitForElement(() => getByLabelText("Close"));
  // close the modal
  fireEvent.click(getByLabelText("Close"));

  const legendButton = getByText("topology-legend");
  expect(legendButton).toBeInTheDocument();

  fireEvent.click(legendButton);
  fireEvent.click(getByLabelText("Close"));

  fireEvent.contextMenu(client);
  fireEvent.click(pfTopologyView);
});
