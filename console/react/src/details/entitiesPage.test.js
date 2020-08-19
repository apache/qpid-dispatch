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
import { render, waitFor, fireEvent } from "@testing-library/react";
import { service, login, TEST_PORT } from "../serviceTest";
import EntitiesPage from "./entitiesPage";

it("renders an EntitiesPage", () => {
  if (!TEST_PORT) {
    console.log("using mock service");
  }
  login(async () => {
    expect(service.management.connection.is_connected()).toBe(true);

    const props = {
      service,
      schema: service.schema,
    };
    let pageRef = null;
    const { getByTestId } = render(
      <EntitiesPage ref={el => (pageRef = el)} {...props} />
    );

    // force page to show the router entity list
    pageRef.handleSelectEntity("router");

    // the router A should be in the list
    await waitFor(() => expect(getByTestId("A")).toBeInTheDocument());

    // click on the A
    fireEvent.click(getByTestId("A"));

    // the details page should show for router A
    await waitFor(() => expect(getByTestId("detail-for-A")).toBeInTheDocument());
  });
});
