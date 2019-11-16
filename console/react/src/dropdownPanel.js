import React from 'react';
import { Accordion, AccordionItem, AccordionContent, AccordionToggle } from '@patternfly/react-core';

class DropdownPanel extends React.Component {
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
  }

  onToggle = () => {
    if (this.state.expanded) {
      this.close();
    } else {
      this.setState({ expanded: true })
    }
  };

  render() {
    return (
      <div
        ref={el => (this.accordionRef = el)}
      >
        <Accordion
          className="dropdown-panel-accordion"
          asDefinitionList>
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
            <AccordionContent
              isHidden={!this.state.expanded}
            >
              <div className="options-panel pf-u-box-shadow-md">
                {this.props.panel}
              </div>
            </AccordionContent>
          </AccordionItem>
        </Accordion>
      </div>
    );
  }
}

export default DropdownPanel;
