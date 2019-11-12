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
        item.endGroup || i === this.props.menuItems.length - 1
          ? " separator"
          : ""
      } ${item.enabled(this.props.contextEventData) ? "" : " disabled"}`;

      return (
        <li
          key={`menu-item-${i}`}
          className={className}
          onClick={e => {
            this.proxyClick(item, e);
          }}
        >
          {item.title}
        </li>
      );
    });

    const style =
      this.props.contextEventPosition[0] >= 0
        ? {
            left: `${this.props.contextEventPosition[0]}px`,
            top: `${this.props.contextEventPosition[1]}px`
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
