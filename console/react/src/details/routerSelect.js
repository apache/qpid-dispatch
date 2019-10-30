import React from "react";
import {
  OptionsMenu,
  OptionsMenuItem,
  OptionsMenuToggleWithText
} from "@patternfly/react-core";
import { utils } from "../amqp/utilities";

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
      const routerName = event.target.id;
      this.setState({ selectedOption: routerName, isOpen: false }, () => {
        this.props.handleRouterSelected(this.nameToId[routerName]);
      });
    };
  }

  componentDidMount = () => {
    this.nodeIdList = this.props.service.management.topology.nodeIdList();
    this.nameToId = {};
    const routers = [];
    this.nodeIdList.forEach(id => {
      const name = utils.nameFromId(id);
      this.nameToId[name] = id;
      routers.push(name);
    });
    this.setState({ routers, selectedOption: routers[0] }, () => {
      this.props.handleRouterSelected(this.nameToId[routers[0]]);
    });
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
      <OptionsMenuToggleWithText
        toggleText={selectedOption}
        onToggle={this.onToggle}
      />
    );

    return (
      <OptionsMenu
        id="routerSelect"
        menuItems={menuItems}
        isOpen={isOpen}
        toggle={toggle}
      />
    );
  }
}

export default RouterSelect;
