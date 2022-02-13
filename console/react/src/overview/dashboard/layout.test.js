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
import { service } from "../../serviceTest";
import PageLayout from "./layout";
import {MemoryRouter} from "react-router-dom";

it("renders the correct title", async () => {
  const title = "Test Layout Page";
  const props = {
    config: { title: title },
    service,
    history: { push: () => {}, replace: () => {} },
    location: { pathname: "/dashboard" }
  };

  const { getAllByText } = render(
    <MemoryRouter initialEntries={["/login"]}>
      <PageLayout {...props} />
    </MemoryRouter>);

  // the correct title should be found
  expect(getAllByText(title));
});

it("calls some utility functions", () => {
  let name = service.utilities.clientName({});
  expect(name).toEqual("client");

  name = service.utilities.clientName({ container: "test-container" });
  expect(name).toEqual("test-container");

  name = service.utilities.clientName({ properties: { product: "test-product" } });
  expect(name).toEqual("test-product");

  let addr = service.utilities.addr_class();
  expect(addr).toEqual("-");
  addr = service.utilities.addr_class("Mtest");
  expect(addr).toEqual("mobile");
  addr = service.utilities.addr_class("Atest");
  expect(addr).toEqual("area");
  addr = service.utilities.addr_class("Rtest");
  expect(addr).toEqual("router");
  addr = service.utilities.addr_class("Ltest");
  expect(addr).toEqual("local");
  addr = service.utilities.addr_class("Htest");
  expect(addr).toEqual("edge");
  addr = service.utilities.addr_class("Ztest");
  expect(addr).toEqual("unknown: Z");

  let sec = service.utilities.connSecurity({});
  expect(sec).toEqual("no-security");
  sec = service.utilities.connSecurity({ isEncrypted: true, sasl: "GSSAPI" });
  expect(sec).toEqual("Kerberos");
  sec = service.utilities.connSecurity({
    isEncrypted: true,
    sasl: "",
    sslProto: "https",
    sslCipher: "test"
  });
  expect(sec).toEqual("https(test)");
});
