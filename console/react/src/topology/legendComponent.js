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

import React, { Component } from "react";
import {
  Accordion,
  AccordionItem,
  AccordionContent,
  AccordionToggle
} from "@patternfly/react-core";

import ArrowsComponent from "./arrowsComponent";
import TrafficComponent from "./trafficComponent";

class LegendComponent extends Component {
  constructor(props) {
    super(props);
    this.state = {};
  }

  render() {
    const toggle = id => {
      const idOpen = this.props[`${id}Open`];
      this.props.handleOpenChange(id, !idOpen);
    };

    return (
      <Accordion>
        <AccordionItem>
          <AccordionToggle
            onClick={() => toggle("traffic")}
            isExpanded={this.props.trafficOpen}
            id="traffic"
          >
            Traffic
          </AccordionToggle>
          <AccordionContent
            id="traffic-expand"
            isHidden={!this.props.trafficOpen}
            isFixed
          >
            <TrafficComponent
              addresses={this.props.addresses}
              addressColors={this.props.addressColors}
              open={this.props.trafficOpen}
              handleChangeTrafficAnimation={
                this.props.handleChangeTrafficAnimation
              }
              handleChangeTrafficFlowAddress={
                this.props.handleChangeTrafficFlowAddress
              }
              dots={this.props.dots}
              congestion={this.props.congestion}
            />
          </AccordionContent>
        </AccordionItem>
        <AccordionItem>
          <AccordionToggle
            onClick={() => toggle("legend")}
            isExpanded={this.props.legendOpen}
            id="legend"
          >
            Legend
          </AccordionToggle>
          <AccordionContent
            id="legend-expand"
            isHidden={!this.props.legendOpen}
            isFixed
          >
            <div id="topo_svg_legend"></div>
          </AccordionContent>
        </AccordionItem>
        <AccordionItem>
          <AccordionToggle
            onClick={() => toggle("map")}
            isExpanded={this.props.mapOpen}
            id="map"
          >
            Background map
          </AccordionToggle>
          <AccordionContent
            id="map-expand"
            isHidden={!this.props.mapOpen}
            isFixed
          >
            <p>Map options go here</p>
          </AccordionContent>
        </AccordionItem>
        <AccordionItem>
          <AccordionToggle
            onClick={() => toggle("arrows")}
            isExpanded={this.props.arrowsOpen}
            id="arrows"
          >
            Arrows
          </AccordionToggle>
          <AccordionContent
            id="arrows-expand"
            isHidden={!this.props.arrowsOpen}
            isFixed
          >
            <ArrowsComponent
              handleChangeArrows={this.props.handleChangeArrows}
              routerArrows={this.props.routerArrows}
              clientArrows={this.props.clientArrows}
            />
          </AccordionContent>
        </AccordionItem>
      </Accordion>
    );
  }
}

export default LegendComponent;
