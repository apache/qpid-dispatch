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
import TopologyToolbar from "./topologyToolbar";

it("renders a TopologyToolbar", () => {
  const props = {
    legendOptions: {
      traffic: {
        addresses: {
          testAddress: true
        },
        dots: true,
        congestion: true
      },
      map: { show: true },
      arrows: { routerArrows: true, clientArrows: true }
    },
    mapOptions: { areaColor: "#EAEAEA", oceanColor: "#EBAEBA" },
    handleChangeTrafficAnimation: () => {},
    handleChangeTrafficFlowAddress: () => {},
    handleHoverAddress: () => {},
    handleUpdateMapColor: () => {},
    handleUpdateMapShown: () => {},
    handleChangeArrows: () => {},
    addressColors: {
      testAddress: { color: "#EAEAEA", checked: true }
    }
  };
  const { getByTestId } = render(<TopologyToolbar {...props} />);

  // the toolbar should be present
  expect(getByTestId("topology-toolbar")).toBeInTheDocument();
});
