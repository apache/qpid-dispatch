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

class ContextMenuComponent extends React.Component {
  constructor(props) {
    super(props);
    this.state = {};
  }

  componentDidMount() {
    document.addEventListener("mousedown", this.handleClickOutside);
  }

  componentWillUnmount() {
    document.removeEventListener("mousedown", this.handleClickOutside);
  }

  handleClickOutside = event => {
    if (this.listRef && this.listRef.contains(event.target)) {
      return;
    }
    this.props.handleContextHide();
  };

  proxyClick = (item, e) => {
    if (item.action && item.enabled(this.props.contextEventData)) {
      item.action(item, this.props.contextEventData, e);
      this.props.handleContextHide();
    }
  };

  render() {
    const menuItems = this.props.menuItems.map((item, i) => {
      let className = `menu-item${
        item.endGroup || i === this.props.menuItems.length - 1 ? " separator" : ""
      } ${item.enabled(this.props.contextEventData) ? "" : " disabled"}`;

      return (
        <li
          aria-label={"context-menu-item"}
          key={`menu-item-${i}`}
          className={className}
          onClick={e => {
            this.proxyClick(item, e);
          }}
        >
          {this.props.suppress && item.suppressIcon && (
            <span
              className={`pficon pficon-${this.props.suppress} menu-item-icon`}
            ></span>
          )}
          {item.title}
        </li>
      );
    });

    const top = Math.max(
      Math.min(
        window.innerHeight - menuItems.length * 35,
        this.props.contextEventPosition[1]
      ),
      0
    );
    const left = Math.max(
      Math.min(window.innerWidth - 200, this.props.contextEventPosition[0]),
      0
    );
    const style =
      this.props.contextEventPosition[0] >= 0
        ? {
            left: `${left}px`,
            top: `${top}px`
          }
        : {};
    return (
      <ul
        className={`context-menu ${this.props.className || ""}`}
        style={style}
        ref={el => (this.listRef = el)}
      >
        {menuItems}
      </ul>
    );
  }
}

export default ContextMenuComponent;
