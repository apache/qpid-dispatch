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
import { render, fireEvent } from "@testing-library/react";
import { service, login, TEST_PORT } from "../serviceTest";
import EntityList from "./entityList";

it("renders a EntityList", () => {
  if (!TEST_PORT) {
    console.log("using mock service");
  }
  login(() => {
    expect(service.management.connection.is_connected()).toBe(true);

    const props = {
      service,
      schema: service.schema,
      handleSelectEntity: () => {}
    };
    const { getByTestId } = render(<EntityList {...props} />);

    // the log item should be there
    const logEntity = getByTestId("log");
    expect(logEntity).toBeInTheDocument();

    // clicking on the log entity should not crash
    fireEvent.click(logEntity);

    fireEvent.click(getByTestId("router"));
    fireEvent.click(getByTestId("autoLink"));
    fireEvent.click(getByTestId("connection"));
  });
});
