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
import { OptionsMenu, OptionsMenuItem, OptionsMenuToggle } from "@patternfly/react-core";
import { utils } from "../common/amqp/utilities";

class RouterSelect extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      isOpen: false,
      selectedOption: "",
      routers: []
    };

    this.onToggle = () => {
      this.setState({
        isOpen: !this.state.isOpen
      });
    };

    this.onSelect = event => {
      const routerName = event.target.textContent;
      this.setState({ selectedOption: routerName, isOpen: false }, () => {
        this.props.handleRouterSelected(this.nameToId[routerName]);
      });
    };
  }

  componentDidMount = () => {
    this.nodeIdList = this.props.service.management.topology.nodeIdList();
    this.nameToId = {};
    const routers = [];
    this.nodeIdList.forEach((id, i) => {
      const name = utils.nameFromId(id);
      this.nameToId[name] = id;
      routers.push(name);
      // find any edge routers connected to this router
      this.props.service.management.topology.fetchEntities(
        id,
        [{ entity: "connection", attrs: ["role", "container"] }],
        ids => {
          for (let rid in ids) {
            const connections = utils.flattenAll(ids[rid].connection);
            // add edge routers
            const edgeIds = connections
              .filter(c => c.role === "edge")
              .map(c => ({
                name: c.container,
                id: utils.idFromName(c.container, "_edge")
              }));
            edgeIds.forEach(e => {
              const edgeName = `${name}-${e.name}`;
              routers.push(edgeName);
              this.nameToId[edgeName] = e.id;
            });
          }
          // last one? then done
          if (i === this.nodeIdList.length - 1 && !this.unmounted) {
            this.setState({ routers, selectedOption: routers[0] }, () => {
              this.props.handleRouterSelected(this.nameToId[routers[0]]);
            });
          }
        }
      );
    });
  };

  componentWillUnmount = () => {
    this.unmounted = true;
  };

  render() {
    const { routers, selectedOption, isOpen } = this.state;

    const menuItems = routers.map(r => (
      <OptionsMenuItem
        onSelect={this.onSelect}
        isSelected={selectedOption === r}
        id={r}
        key={`key-${r}`}
      >
        {r}
      </OptionsMenuItem>
    ));

    const toggle = (
      <OptionsMenuToggle onToggle={this.onToggle} toggleTemplate={selectedOption} />
    );

    return (
      <OptionsMenu
        id="options-menu-single-option-example"
        menuItems={menuItems}
        isOpen={isOpen}
        toggle={toggle}
      />
    );
  }
}

export default RouterSelect;
