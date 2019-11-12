import React from "react";
import { TextContent, Text, TextVariants } from "@patternfly/react-core";
import PropTypes from "prop-types";

import { CogIcon } from "@patternfly/react-icons";

class PleaseWait extends React.Component {
  static propTypes = {
    isOpen: PropTypes.bool.isRequired,
    title: PropTypes.string.isRequired,
    message: PropTypes.string.isRequired
  };

  state = {};

  render() {
    return (
      this.props.isOpen && (
        <div className="topic-creating-wrapper">
          <div id="topicCogWrapper">
            <CogIcon
              id="topicCogMain"
              className="spinning-clockwise"
              color="#AAAAAA"
            />
            <CogIcon
              id="topicCogUpper"
              className="spinning-cclockwise"
              color="#AAAAAA"
            />
            <CogIcon
              id="topicCogLower"
              className="spinning-cclockwise"
              color="#AAAAAA"
            />
          </div>
          <TextContent>
            <Text component={TextVariants.h3}>{this.props.title}</Text>
          </TextContent>
          <TextContent>
            <Text className="topic-creating-message" component={TextVariants.p}>
              {this.props.message}
            </Text>
          </TextContent>
        </div>
      )
    );
  }
}

export default PleaseWait;
