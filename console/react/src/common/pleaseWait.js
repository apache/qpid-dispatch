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
            <CogIcon id="topicCogMain" className="spinning-clockwise" color="#AAAAAA" />
            <CogIcon id="topicCogUpper" className="spinning-cclockwise" color="#AAAAAA" />
            <CogIcon id="topicCogLower" className="spinning-cclockwise" color="#AAAAAA" />
          </div>
          <TextContent>
            <Text component={TextVariants.p}>{this.props.title}</Text>
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
