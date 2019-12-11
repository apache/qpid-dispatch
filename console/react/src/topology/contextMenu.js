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
import ContextMenuComponent from "../common/contextMenuComponent";

class ContextMenu extends Component {
  constructor(props) {
    super(props);
    this.state = {};
    this.contextMenuItems = [
      {
        title: "Freeze in place",
        action: this.setFixed,
        enabled: data => !this.isFixed(data)
      },
      {
        title: "Unfreeze",
        action: this.setFixed,
        enabled: this.isFixed,
        endGroup: true
      },
      {
        title: "Unselect",
        action: this.props.setSelected,
        enabled: this.isSelected
      },
      {
        title: "Select",
        action: this.props.setSelected,
        enabled: data => !this.isSelected(data),
        endGroup: true
      }
    ];
    // on a router or edge group, allow separate/collapse edges
    if (
      this.props.contextEventData.nodeType === "_topo" ||
      this.props.contextEventData.nodeType === "edge"
    ) {
      const additional = [];
      if (this.props.canExpandAll) {
        additional.push({
          title: "Expand all edges",
          action: this.props.separateAllEdges,
          enabled: () => true
        });
      }
      if (this.props.canCollapseAll) {
        additional.push({
          title: "Collapse all edges",
          action: this.props.collapseAllEdges,
          enabled: () => true
        });
      }
      // append expand/collapse items
      this.contextMenuItems = [...this.contextMenuItems, ...additional];
    }
  }

  setFixed = (item, data) => {
    data.setFixed(item.title !== "Unfreeze");
  };

  isFixed = data => {
    return data.isFixed();
  };

  isSelected = data => {
    return data.selected ? true : false;
  };

  render() {
    return (
      <ContextMenuComponent
        contextEventPosition={this.props.contextEventPosition}
        contextEventData={this.props.contextEventData}
        handleContextHide={this.props.handleContextHide}
        menuItems={this.contextMenuItems}
      />
    );
  }
}

export default ContextMenu;
