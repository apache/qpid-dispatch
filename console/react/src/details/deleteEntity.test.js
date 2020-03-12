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
import { service, login } from "../serviceTest";
import DeleteEntity from "./deleteEntity";

it("renders DeleteEntity", () => {
  const entity = "listener";
  const routerName = "A";
  const recordName = "testListener";

  login(() => {
    expect(service.management.connection.is_connected()).toBe(true);

    const props = {
      entity,
      service,
      record: {
        name: recordName,
        routerId: service.utilities.idFromName(routerName, "_topo"),
        identity: `${entity}/:amqp:${recordName}`
      },
      handleAddNotification: () => {}
    };

    const { getByLabelText } = render(<DeleteEntity {...props} />);
    const button = getByLabelText("delete-entity-button");
    expect(button).toBeInTheDocument();

    // so the delete confirmation popup
    fireEvent.click(button);
    const confirmButton = getByLabelText("confirm-delete");
    expect(confirmButton).toBeInTheDocument();

    // try to delete
    fireEvent.click(confirmButton);
  });
});
