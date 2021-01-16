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
import {mockService} from "../../test_data/qdrService.mock";
import {BackgroundMap} from "./map";
import * as d3 from "d3";
import * as world from "../../public/data/countries.json";

it("renders the world map", async () => {
  const props = {
    service: mockService({})
  };
  const map = new BackgroundMap(
      this,
      // this.state.legendOptions.map,
      {"show": true},
      // notify: called each time a pan/zoom is performed
      () => {
        // if (this.state.legendOptions.map.show) {
        //   // set all the nodes' x,y position based on their saved lon,lat
        //   this.forceData.nodes.setXY(this.backgroundMap);
        //   this.forceData.nodes.savePositions();
        //   // redraw the nodes in their x,y position and let non-fixed nodes bungie
        //   this.force.start();
        //   this.clearPopups();
        // }
      }
  );

  jest.spyOn(window, "fetch").mockImplementationOnce(() => {
    return Promise.resolve({
      json: () => Promise.resolve(world)
    });
  });

  document.body.innerHTML ="<div id='topology'>";
  let svg = d3
      .select("#topology")
      .append("svg")
      .attr("id", "SVG_ID")
      .attr("xmlns", "http://www.w3.org/2000/svg")
      .attr("width", 1)
      .attr("height", 1)

  await map.init('_unused', svg, 1, 1);
  map.setMapOpacity(true);
  map.setWidthHeight(1, 1);
  map.restartZoom();
  // make sure it rendered the component
  // expect(getByTestId("topology-page")).toBeInTheDocument();
});
