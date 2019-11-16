import React from "react";
import { render } from '@testing-library/react';
import ConnectForm from "./connect-form";
import { QDRService } from "./qdrService";

it("can create a service object", () => {
  const service = new QDRService(() => { });
});

it("renders the connect form", () => {
  render(
    <ConnectForm />
  );
});

it("connect form can be submitted", () => {
  let connectFormRef = null;
  const service = new QDRService(() => { });
  const handleConnect = (fromPath, r) => { };
  const handleAddNotification = (section, message, date, severity) => { }
  render(
    <ConnectForm
      ref={el => (connectFormRef = el)}
      service={service}
      handleConnect={handleConnect}
      handleAddNotification={handleAddNotification}
      fromPath={"/test"}
    />
  );
  connectFormRef.handleConnect();
});
