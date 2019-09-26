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
import OptionsComponent from "./optionsComponent";
import RoutersComponent from "./routersComponent";
import AddressesComponent from "../addressesComponent";

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
      <Accordion className="legend">
        <AccordionItem>
          <AccordionToggle
            onClick={() => toggle("options")}
            isExpanded={this.props.optionsOpen}
            id="options"
          >
            Options
          </AccordionToggle>
          <AccordionContent
            id="options-expand"
            isHidden={!this.props.optionsOpen}
            isFixed
          >
            <OptionsComponent
              isRate={this.props.isRate}
              byAddress={this.props.byAddress}
              handleChangeOption={this.props.handleChangeOption}
            />
          </AccordionContent>
        </AccordionItem>
        <AccordionItem>
          <AccordionToggle
            onClick={() => toggle("routers")}
            isExpanded={this.props.routersOpen}
            id="routers"
          >
            Routers
          </AccordionToggle>
          <AccordionContent
            id="routers-expand"
            isHidden={!this.props.routersOpen}
            isFixed
          >
            <RoutersComponent
              arcColors={this.props.arcColors}
              handleHoverRouter={this.props.handleHoverRouter}
            />
          </AccordionContent>
        </AccordionItem>
        <AccordionItem>
          <AccordionToggle
            onClick={() => toggle("addresses")}
            isExpanded={this.props.addressesOpen}
            id="addresses"
          >
            Addresses
          </AccordionToggle>
          <AccordionContent
            id="addresses-expand"
            isHidden={!this.props.addressesOpen}
            isFixed
          >
            <AddressesComponent
              addresses={this.props.addresses}
              addressColors={this.props.chordColors}
              handleChangeAddress={this.props.handleChangeAddress}
              handleHoverAddress={this.props.handleHoverAddress}
            />
          </AccordionContent>
        </AccordionItem>
      </Accordion>
    );
  }
}

export default LegendComponent;
