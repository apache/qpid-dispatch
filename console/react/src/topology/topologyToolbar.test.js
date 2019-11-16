import React from "react";
import { render } from '@testing-library/react';
import TopologyToolbar from "./topologyToolbar";

it('renders a TopologyToolbar', () => {
  const props = {
    legendOptions: {
      traffic: {
        addresses: {
          testAddress: true
        },
        addressColors: {
          testAddress: "#EAEAEA"
        },
        dots: true,
        congestion: true
      },
      map: { show: true },
      arrows: { routerArrows: true, clientArrows: true }
    },
    mapOptions: { areaColor: "#EAEAEA", oceanColor: "#EBAEBA" },
    handleChangeTrafficAnimation: () => { },
    handleChangeTrafficFlowAddress: () => { },
    handleHoverAddress: () => { },
    handleUpdateMapColor: () => { },
    handleUpdateMapShown: () => { },
    handleChangeArrows: () => { }
  }
  const {
    getByTestId
  } = render(<TopologyToolbar {...props} />);

  // the toolbar should be present
  expect(getByTestId("topology-toolbar")).toBeInTheDocument();

})
