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
import { service, login, TEST_PORT } from "../../serviceTest";
import throughputChartData from "./throughputData";
import inflightChartData from "./inflightData";
import DashboardPage from "./dashboardPage";

it("renders the DashboardPage component", () => {
  const props = {
    service,
    throughputChartData: new throughputChartData(service),
    inflightChartData: new inflightChartData(service)
  };

  if (!TEST_PORT) console.log("using mock service");
  login(() => {
    expect(service.management.connection.is_connected()).toBe(true);

    const { getByLabelText } = render(<DashboardPage {...props} />);

    // make sure it rendered the component
    expect(getByLabelText("dashboard-page")).toBeInTheDocument();
  });
});
