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
import {
  Accordion,
  AccordionItem,
  AccordionContent,
  AccordionToggle
} from "@patternfly/react-core";
import PropTypes from "prop-types";

class DropdownPanel extends React.Component {
  static propTypes = {
    title: PropTypes.string.isRequired,
    panel: PropTypes.object.isRequired
  };
  constructor(props) {
    super(props);
    this.state = {
      expanded: false
    };
  }

  componentDidMount() {
    document.addEventListener("mousedown", this.handleClickOutside);
  }

  componentWillUnmount() {
    document.removeEventListener("mousedown", this.handleClickOutside);
  }

  handleClickOutside = event => {
    if (this.accordionRef && !this.accordionRef.contains(event.target)) {
      this.close();
    }
  };

  close = () => {
    this.setState({ expanded: false });
  };

  onToggle = () => {
    if (this.state.expanded) {
      this.close();
    } else {
      this.setState({ expanded: true });
    }
  };

  render() {
    return (
      <div ref={el => (this.accordionRef = el)}>
        <Accordion className="dropdown-panel-accordion" asDefinitionList>
          <AccordionItem>
            <AccordionToggle
              id={this.props.title}
              className="dropdown-panel-toggle"
              onClick={this.onToggle}
              isExpanded={this.state.expanded}
              aria-label={`button-for-${this.props.title}`}
            >
              {this.props.title}
            </AccordionToggle>
            <AccordionContent isHidden={!this.state.expanded}>
              <div className="options-panel pf-u-box-shadow-md">{this.props.panel}</div>
            </AccordionContent>
          </AccordionItem>
        </Accordion>
      </div>
    );
  }
}

export default DropdownPanel;
