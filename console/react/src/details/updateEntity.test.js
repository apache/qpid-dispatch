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
import UpdateEntity from "./updateEntity";

it("renders UpdateEntity", () => {
  let buttonClicked = false;
  const props = {
    handleEntityAction: () => {
      buttonClicked = true;
    }
  };

  const { getByLabelText } = render(<UpdateEntity {...props} />);
  const button = getByLabelText("update-entity-button");
  expect(button).toBeInTheDocument();

  fireEvent.click(button);
  expect(buttonClicked).toBe(true);
});
