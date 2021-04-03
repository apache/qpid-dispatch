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
import { service, login, TEST_PORT } from "../serviceTest";
import { LocalStorageMock } from "@react-mock/localstorage";
import ChordViewer from "./chordViewer";

it("renders the ChordViewer component", (done) => {
  const props = {
    service,
  };

  if (!TEST_PORT) {
    console.log("using mock service");
  }
  login(async () => {
    expect(service.management.connection.is_connected()).toBe(true);

    const { getByLabelText } = render(
      <LocalStorageMock
        items={{
          chordOptions: JSON.stringify({
            isRate: false,
            byAddress: true,
          }),
        }}
      >
        <ChordViewer {...props} />
      </LocalStorageMock>
    );

    // make sure it rendered the component
    const pfTopologyView = getByLabelText("chord-viewer");
    expect(pfTopologyView).toBeInTheDocument();

    // make sure it created the svg
    expect(getByLabelText("chord-svg")).toBeInTheDocument();

    const optionsButton = getByLabelText("button-for-Options");
    fireEvent.click(optionsButton);

    // turn on show by address
    const addressButton = getByLabelText("show by address");
    fireEvent.click(addressButton);

    // after 1 update period, the B chord should be in the svg
    await waitFor(() => expect(getByLabelText("B")).toBeInTheDocument());

    done();
  })
});
