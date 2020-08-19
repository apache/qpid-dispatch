/*
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
*/

import React from "react";
import { render, fireEvent, waitFor } from "@testing-library/react";
import { service, login } from "../serviceTest";
import TopologyViewer from "./topologyViewer";
import * as world from "../../public/data/countries.json";

it("renders the TopologyViewer component", () => {
  jest.spyOn(window, "fetch").mockImplementationOnce(() => {
    return Promise.resolve({
      json: () => Promise.resolve(world),
    });
  });
  const props = {
    service,
    handleAddNotification: () => {},
  };

  login(async () => {
    const { getByLabelText, getByText, getByTestId } = render(
      <TopologyViewer {...props} />
    );

    // make sure it rendered the component
    const pfTopologyView = getByLabelText("topology-viewer");
    expect(pfTopologyView).toBeInTheDocument();
    expect(getByLabelText("topology-diagram")).toBeInTheDocument();

    // make sure it created the svg
    //await waitFor(() => getByLabelText("topology-svg"));
    await waitFor(() => expect(getByLabelText("topology-svg").toBeInTheDocument()));

    // the svg should have a router circle
    await waitFor(() => expect(getByTestId("router-0")).toBeInTheDocument());

    // "click" on the router
    const router = getByTestId("router-0");
    fireEvent.mouseDown(router);
    fireEvent.mouseUp(router);

    // the routerInfo modal appears
    await waitFor(() => expect(getByLabelText("Close")).toBeInTheDocument());
    // close the modal
    fireEvent.click(getByLabelText("Close"));

    // the svg should have a client circle
    const client = getByTestId("client-2");
    // "click" on the client
    fireEvent.mouseDown(client);
    fireEvent.mouseUp(client);

    // the clientInfo modal appears
    await waitFor(() => expect(getByLabelText("Close")).toBeInTheDocument());
    // close the modal
    fireEvent.click(getByLabelText("Close"));

    // make sure the legend opens
    const legendButton = getByText("topology-legend");
    expect(legendButton).toBeInTheDocument();
    fireEvent.click(legendButton);
    fireEvent.click(getByLabelText("Close"));

    // turn on the traffic animation

    // dropdown the traffic panel
    fireEvent.contextMenu(client);
    fireEvent.click(pfTopologyView);
    const trafficButton = getByLabelText("button-for-Traffic");
    expect(trafficButton).toBeInTheDocument();
    fireEvent.click(trafficButton);

    // click on the show traffic checkbox
    const trafficCheckbox = getByLabelText("show traffic by address");
    fireEvent.click(trafficCheckbox);
    // the address dot should be there
    await waitFor(() => expect(getByText("toB")).toBeInTheDocument());
  });
});
