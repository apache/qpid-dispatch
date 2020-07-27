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
import InflightChart from "./inflightChart";
import InflightChartData from "./inflightData";

it("renders the InflightChart component", (done) => {
  // the chart only renders if parent's clientWidth != 0
  Object.defineProperty(window.HTMLDivElement.prototype, 'clientWidth', {value: 1});

  const props = {
    service,
    chartData: new InflightChartData(service)
  };

  if (!TEST_PORT) console.log("using mock service");
  login(() => {
    expect(service.management.connection.is_connected()).toBe(true);

    const { getByLabelText } = render(<InflightChart {...props} />);
    // make sure it rendered the component
    expect(getByLabelText("Messages in flight")).toBeInTheDocument();

    done()
  });
});
