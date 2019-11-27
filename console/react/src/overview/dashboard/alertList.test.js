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
import { render } from "@testing-library/react";
import AlertList from "./alertList";

it("renders the AlertList component", async () => {
  let ref = null;
  const props = {};
  const { getByLabelText, queryByLabelText } = render(
    <AlertList ref={el => (ref = el)} {...props} />
  );
  // the container should be there
  expect(getByLabelText("alert-list")).toBeInTheDocument();
  // there should be no alerts in the list to start with
  expect(queryByLabelText("alert-close-button")).toBeNull();

  // add an alert
  ref.addAlert("info", "testing");
  // the alert close button should now be there
  expect(getByLabelText("alert-close-button")).toBeInTheDocument();

  const alert = {
    key: 0
  };
  // hide the alert
  ref.hideAlert(alert);
  // the alert close button should now be gone
  // TODO: The alert fades out over 5 seconds. Find a way to test that.
  //expect(queryByLabelText("alert-close-button")).toBeNull();
});
