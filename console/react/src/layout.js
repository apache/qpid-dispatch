import React from "react";
import {
  Avatar,
  Button,
  ButtonVariant,
  Dropdown,
  DropdownToggle,
  DropdownItem,
  DropdownSeparator,
  KebabToggle,
  Page,
  PageHeader,
  SkipToContent,
  Toolbar,
  ToolbarGroup,
  ToolbarItem,
  Nav,
  NavExpandable,
  NavItem,
  NavList,
  PageSidebar
} from "@patternfly/react-core";
// make sure you've installed @patternfly/patternfly
import accessibleStyles from "@patternfly/patternfly/utilities/Accessibility/accessibility.css";
import spacingStyles from "@patternfly/patternfly/utilities/Spacing/spacing.css";
import { css } from "@patternfly/react-styles";
import { BellIcon, CogIcon } from "@patternfly/react-icons";
import ShowD3SVG from "./show-d3-svg";
import ConnectForm from "./connect-form";
import ConnectPage from "./connectPage";
import OverviewChartsPage from "./overviewChartsPage";
import OverviewTablePage from "./overviewTablePage";
import TopologyPage from "./topology/qdrTopology";
import { QDRService } from "./qdrService";
const avatarImg = require("./assets/img_avatar.svg");

class PageLayout extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      connected: false,
      isDropdownOpen: false,
      isKebabDropdownOpen: false,
      activeGroup: "overview",
      activeItem: "charts"
    };
    this.tableInfo = {
      routers: [
        { fieldName: "name", displayName: "Router" },
        { name: "role" },
        { name: "mode" },
        {
          fieldName: "addresses",
          displayName: "Addresses",
          getter: this.getAddresses
        }
      ]
    };
    this.hooks = { setLocation: this.setLocation };
    this.service = new QDRService(this.hooks);
  }

  setLocation = where => {
    //console.log(`setLocation to ${where}`);
  };
  getAddresses = (field, data) => {
    return new Promise((resolve, reject) => {
      resolve("2");
    });
  };

  onDropdownToggle = isDropdownOpen => {
    this.setState({
      isDropdownOpen
    });
  };

  onDropdownSelect = event => {
    this.setState({
      isDropdownOpen: !this.state.isDropdownOpen
    });
  };

  onKebabDropdownToggle = isKebabDropdownOpen => {
    this.setState({
      isKebabDropdownOpen
    });
  };

  onKebabDropdownSelect = event => {
    this.setState({
      isKebabDropdownOpen: !this.state.isKebabDropdownOpen
    });
  };

  handleConnect = event => {
    this.service
      .connect({ address: "localhost", port: 5673, reconnect: true })
      .then(
        r => {
          //console.log(r);
        },
        e => {
          console.log(e);
        }
      );
    this.setState({
      connected: true
    });
  };

  onNavSelect = result => {
    this.setState({
      activeItem: result.itemId,
      activeGroup: result.groupId
    });
  };

  render() {
    const {
      isDropdownOpen,
      isKebabDropdownOpen,
      activeItem,
      activeGroup
    } = this.state;

    const PageNav = (
      <Nav onSelect={this.onNavSelect} aria-label="Nav">
        <NavList>
          <NavExpandable
            title="Overview"
            groupId="overview"
            isActive={activeGroup === "overview"}
            isExpanded
          >
            <NavItem
              groupId="overview"
              itemId="charts"
              isActive={activeItem === "charts"}
            >
              Charts
            </NavItem>
            <NavItem
              groupId="overview"
              itemId="routers"
              isActive={activeItem === "routers"}
            >
              Routers
            </NavItem>
            <NavItem
              groupId="overview"
              itemId="addresses"
              isActive={activeItem === "addresses"}
            >
              Addresses
            </NavItem>
            <NavItem
              groupId="overview"
              itemId="links"
              isActive={activeItem === "links"}
            >
              Links
            </NavItem>
            <NavItem
              groupId="overview"
              itemId="connections"
              isActive={activeItem === "connections"}
            >
              Connections
            </NavItem>
            <NavItem
              groupId="overview"
              itemId="logs"
              isActive={activeItem === "logs"}
            >
              Logs
            </NavItem>
          </NavExpandable>
          <NavExpandable
            title="Visualizations"
            groupId="visualizations"
            isActive={activeGroup === "visualizations"}
          >
            <NavItem
              groupId="visualizations"
              itemId="topology"
              isActive={activeItem === "topology"}
            >
              Topology
            </NavItem>
            <NavItem
              groupId="visualizations"
              itemId="flow"
              isActive={activeItem === "flow"}
            >
              Message flow
            </NavItem>
          </NavExpandable>
          <NavExpandable
            title="Details"
            groupId="grp-3"
            isActive={activeGroup === "grp-3"}
          >
            <NavItem
              groupId="grp-3"
              itemId="grp-3_itm-1"
              isActive={activeItem === "grp-3_itm-1"}
            >
              Entities
            </NavItem>
            <NavItem
              groupId="grp-3"
              itemId="grp-3_itm-2"
              isActive={activeItem === "grp-3_itm-2"}
            >
              Schema
            </NavItem>
          </NavExpandable>
        </NavList>
      </Nav>
    );
    const kebabDropdownItems = [
      <DropdownItem key="notif">
        <BellIcon /> Notifications
      </DropdownItem>,
      <DropdownItem key="sett">
        <CogIcon /> Settings
      </DropdownItem>
    ];
    const userDropdownItems = [
      <DropdownItem key="link">Link</DropdownItem>,
      <DropdownItem component="button" key="action">
        Action
      </DropdownItem>,
      <DropdownItem isDisabled key="dis">
        Disabled Link
      </DropdownItem>,
      <DropdownItem isDisabled component="button" key="button">
        Disabled Action
      </DropdownItem>,
      <DropdownSeparator key="sep0" />,
      <DropdownItem key="sep">Separated Link</DropdownItem>,
      <DropdownItem component="button" key="sep1">
        Separated Action
      </DropdownItem>
    ];
    const PageToolbar = (
      <Toolbar>
        <ToolbarGroup
          className={css(
            accessibleStyles.screenReader,
            accessibleStyles.visibleOnLg
          )}
        >
          <ToolbarItem>
            <ConnectForm prefix="toolbar" handleConnect={this.handleConnect} />
          </ToolbarItem>
          <ToolbarItem>
            <Button
              id="default-example-uid-01"
              aria-label="Notifications actions"
              variant={ButtonVariant.plain}
            >
              <BellIcon />
            </Button>
          </ToolbarItem>
          <ToolbarItem>
            <Button
              id="default-example-uid-02"
              aria-label="Settings actions"
              variant={ButtonVariant.plain}
            >
              <CogIcon />
            </Button>
          </ToolbarItem>
        </ToolbarGroup>
        <ToolbarGroup>
          <ToolbarItem
            className={css(accessibleStyles.hiddenOnLg, spacingStyles.mr_0)}
          >
            <Dropdown
              isPlain
              position="right"
              onSelect={this.onKebabDropdownSelect}
              toggle={<KebabToggle onToggle={this.onKebabDropdownToggle} />}
              isOpen={isKebabDropdownOpen}
              dropdownItems={kebabDropdownItems}
            />
          </ToolbarItem>
          <ToolbarItem
            className={css(
              accessibleStyles.screenReader,
              accessibleStyles.visibleOnMd
            )}
          >
            <Dropdown
              isPlain
              position="right"
              onSelect={this.onDropdownSelect}
              isOpen={isDropdownOpen}
              toggle={
                <DropdownToggle onToggle={this.onDropdownToggle}>
                  anonymous
                </DropdownToggle>
              }
              dropdownItems={userDropdownItems}
            />
          </ToolbarItem>
        </ToolbarGroup>
      </Toolbar>
    );

    const Header = (
      <PageHeader
        className="topology-header"
        logo={
          <React.Fragment>
            <ShowD3SVG
              className="topology-logo"
              topology="ted"
              routers={6}
              center={false}
              dimensions={{ width: 200, height: 75 }}
              radius={6}
              thumbNail={true}
              notifyCurrentRouter={() => {}}
            />
            <span className="logo-text">Apache Qpid Dispatch Console</span>
          </React.Fragment>
        }
        toolbar={PageToolbar}
        avatar={<Avatar src={avatarImg} alt="Avatar image" />}
      />
    );
    const Sidebar = <PageSidebar nav={PageNav} className="qdr-sidebar" />;
    const pageId = "main-content-page-layout-expandable-nav";
    const PageSkipToContent = (
      <SkipToContent href={`#${pageId}`}>Skip to Content</SkipToContent>
    );
    const activeItemToPage = () => {
      if (this.state.activeGroup === "overview") {
        if (this.state.activeItem === "charts") {
          return <OverviewChartsPage />;
        }
        return (
          <OverviewTablePage
            entity={this.state.activeItem}
            tableInfo={this.tableInfo[this.state.activeItem]}
          />
        );
      } else if (this.state.activeGroup === "visualizations") {
        return <TopologyPage service={this.service} />;
      }
      //console.log("using overview charts page");
      return <OverviewChartsPage />;
    };

    if (!this.state.connected) {
      return (
        <Page header={Header} skipToContent={PageSkipToContent}>
          <ConnectPage handleConnect={this.handleConnect} />
        </Page>
      );
    }

    return (
      <React.Fragment>
        <Page
          header={Header}
          sidebar={Sidebar}
          isManagedSidebar
          skipToContent={PageSkipToContent}
        >
          {activeItemToPage()}
        </Page>
      </React.Fragment>
    );
  }
}

export default PageLayout;
