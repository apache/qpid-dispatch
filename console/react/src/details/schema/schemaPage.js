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
import { PageSection, PageSectionVariants } from "@patternfly/react-core";
import { Stack, StackItem, TextContent, Text, TextVariants } from "@patternfly/react-core";
import { Card, CardBody } from "@patternfly/react-core";

class SchemaPage extends React.Component {
  constructor(props) {
    super(props);

    this.state = {
      root: {
        key: "entities",
        title: "Schema entities",
        description: "List of management entities. Click on an entity to view its attributes.",
        hidden: false
      }
    };
    this.initRoot(this.state.root, this.props.schema.entityTypes);
  }

  initRoot = (root, schema) => {
    root.attributes = [
      {
        key: Object.keys(schema).length,
        value: "Entities"
      }
    ];
    root.children = [];
    const entities = Object.keys(schema).sort();
    for (let i = 0; i < entities.length; i++) {
      const entity = entities[i];
      const child = { title: entity, key: entity };
      this.initChild(child, schema[entity]);
      root.children.push(child);
    }
  };

  initChild = (child, obj) => {
    child.hidden = true;
    child.description = obj.description;
    child.attributes = [];
    if (obj.attributes) {
      child.hidden = false;
      child.attributes.push({
        key: Object.keys(obj.attributes).length,
        value: "Attributes"
      });
      child.children = [];
      for (const attr in obj.attributes) {
        const sub = { title: attr, key: `${child.key}-${attr}` };
        this.initChild(sub, obj.attributes[attr]);
        child.children.push(sub);
      }
    }
    if (obj.operations) {
      child.attributes.push({
        key: "Operations",
        value: `[${obj.operations.join(", ")}]`
      });
    }
    if (obj.fullyQualifiedType) {
      child.fqt = obj.fullyQualifiedType;
    }
    if (obj.type) {
      child.attributes.push({
        key: "type",
        value: obj.type.constructor === Array ? `[${obj.type.join(", ")}]` : obj.type
      });
    }
    if (obj.default) {
      child.attributes.push({ key: "default", value: obj.default });
    }
    if (obj.required) {
      child.attributes.push({ key: "required", value: "" });
    }
    if (obj.unique) {
      child.attributes.push({ key: "unique", value: "" });
    }
    if (obj.graph) {
      child.attributes.push({ key: "statistic", value: "" });
    }
  };

  toggleChildren = (event, parent) => {
    event.stopPropagation();
    if (parent.children && parent.children.length > 0) {
      parent.children.forEach(child => {
        child.hidden = !child.hidden;
      });
      this.setState({ root: this.state.root });
    }
  };

  folderIconClass = item => {
    if (item.children) {
      return item.children.some(child => !child.hidden)
        ? "pficon-folder-open"
        : "pficon-folder-close";
    }
    return "pficon-catalog";
  };

  attrIconClass = attr => {
    const attrMap = {
      type: "pficon-builder-image",
      Operations: "pficon-maintenance",
      default: "pficon-info",
      unique: "pficon-locked",
      statistic: "fa fa-icon fa-tachometer"
    };
    if (attrMap[attr.key]) return `pficon ${attrMap[attr.key]}`;
    return "pficon pficon-repository";
  };

  render() {
    const TreeItem = itemInfo => {
      return (
        !itemInfo.hidden && (
          <div
            key={itemInfo.key}
            data-testid={itemInfo.key}
            className={`list-group-item-container container-fluid`}
            onClick={event => this.toggleChildren(event, itemInfo)}
          >
            <div className="list-group-item">
              <div className="list-group-item-header">
                <div className="list-view-pf-main-info">
                  <div className="list-view-pf-left">
                    <span
                      className={`pficon ${this.folderIconClass(itemInfo)} list-view-pf-icon-sm`}
                    ></span>
                  </div>
                  <div className="list-view-pf-body">
                    <div className="list-view-pf-description">
                      <div className="list-group-item-heading">{itemInfo.title}</div>
                      <div className="list-group-item-text">
                        {itemInfo.description}
                        {itemInfo.fqt && <div className="list-group-item-fqt">{itemInfo.fqt}</div>}
                      </div>
                    </div>
                    <div className="list-view-pf-additional-info">
                      {itemInfo.attributes &&
                        itemInfo.attributes.map((attr, i) => (
                          <div
                            className="list-view-pf-additional-info-item"
                            key={`${itemInfo.key}-${i}`}
                          >
                            <span className={this.attrIconClass(attr)}></span>
                            <strong>{attr.key}</strong>
                            {attr.value}
                          </div>
                        ))}
                    </div>
                  </div>
                </div>
              </div>
              {itemInfo.children && itemInfo.children.map(childInfo => TreeItem(childInfo))}
            </div>
          </div>
        )
      );
    };

    return (
      <PageSection variant={PageSectionVariants.light} id="schema-page">
        <Stack>
          <StackItem>
            <TextContent>
              <Text className="overview-title" component={TextVariants.h1}>
                Schema
              </Text>
            </TextContent>
          </StackItem>
          <StackItem>
            <Card>
              <CardBody>
                <div className="container-fluid">
                  <div className="list-group tree-list-view-pf" data-testid="root">
                    {TreeItem(this.state.root)}
                  </div>
                </div>
              </CardBody>
            </Card>
          </StackItem>
        </Stack>
      </PageSection>
    );
  }
}

export default SchemaPage;
