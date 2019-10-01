import React from "react";

class ContextMenuComponent extends React.Component {
  constructor(props) {
    super(props);
    this.state = {};
  }

  componentDidMount = () => {
    this.registerHandlers();
  };

  componentWillUnmount = () => {
    this.unregisterHandlers();
  };

  registerHandlers = () => {
    document.addEventListener("mousedown", this.handleOutsideClick);
    document.addEventListener("touchstart", this.handleOutsideClick);
    document.addEventListener("scroll", this.handleHide);
    document.addEventListener("contextmenu", this.handleContextMenuEvent);
    window.addEventListener("resize", this.handleHide);
  };

  unregisterHandlers = () => {
    document.removeEventListener("mousedown", this.handleOutsideClick);
    document.removeEventListener("touchstart", this.handleOutsideClick);
    document.removeEventListener("scroll", this.handleHide);
    document.removeEventListener("contextmenu", this.handleContextMenuEvent);
    window.removeEventListener("resize", this.handleHide);
  };

  handleHide = e => {
    this.unregisterHandlers();
    this.props.handleContextHide();
  };

  handleContextMenuEvent = e => {
    // if the event happened to an svg circle, don't hide the context menu
    if (!e.target || e.target.nodeName !== "circle") {
      this.handleHide(e);
    }
  };

  handleOutsideClick = e => {
    if (e.target.nodeName !== "LI") {
      this.handleHide(e);
    }
  };

  proxyClick = (item, e) => {
    if (item.action && item.enabled(this.props.contextEventData)) {
      item.action(item, this.props.contextEventData, e);
      this.handleHide(e);
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

    const style = {
      left: `${this.props.contextEventPosition[0]}px`,
      top: `${this.props.contextEventPosition[1]}px`
    };
    return (
      <ul className="context-menu" style={style}>
        {menuItems}
      </ul>
    );
  }
}

export default ContextMenuComponent;
