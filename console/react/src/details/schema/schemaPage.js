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
import { TreeView, Button } from "@patternfly/react-core";

class SchemaPage extends React.Component {
  constructor(props) {
    super(props);
    this.state = { activeItems: [], allExpanded: false };
    this.nextId = 0;
    this.options = this.buildTreeOptions();
  }

  handleTreeClick = (evt, treeViewItem, parentItem) => {
    this.setState({
      activeItems: [treeViewItem, parentItem],
    });
  };

  onToggle = evt => {
    const { allExpanded } = this.state;
    this.setState({
      allExpanded: allExpanded !== undefined ? !allExpanded : true,
    });
  };

  handleExpandAllToggle = (evt, treeViewItem, parentItem) => {
    this.handleTreeClick(evt, treeViewItem, parentItem);
  };

  buildTreeOptions = () => {
    const schema = this.props.schema.entityTypes;
    const options = [];
    const entities = Object.keys(schema).sort();
    entities.forEach(entity => {
      this.initChild(options, entity, schema);
    });
    return options;
  };

  initChild = (parent, entity, schema) => {
    const child = { name: entity, id: `ID.${this.nextId++}` };
    const keys = Object.keys(schema[entity]);
    keys.forEach((key, i) => {
      const isArray = Array.isArray(schema[entity][key]);
      if (typeof schema[entity][key] === "object" && !isArray) {
        if (!child.children) {
          child.children = [];
        }
        this.initChild(child.children, key, schema[entity]);
      } else {
        if (!child.children) {
          child.children = [];
        }
        child.children.push({
          name: `${key}: ${
            isArray ? schema[entity][key].join(", ") : schema[entity][key]
          }`,
          id: `ID.${this.nextId++}`,
        });
      }
    });
    parent.push(child);
  };

  render() {
    const { activeItems, allExpanded } = this.state;
    return (
      <React.Fragment>
        <Button variant="link" onClick={this.onToggle}>
          {allExpanded && "Collapse all"}
          {!allExpanded && "Expand all"}
        </Button>
        <TreeView
          data={this.options}
          activeItems={activeItems}
          onSelect={this.handleTreeClick}
          allExpanded={allExpanded}
        />
      </React.Fragment>
    );
  }
}

export default SchemaPage;
