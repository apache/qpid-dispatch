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

import accessibleStyles from "@patternfly/patternfly/utilities/Accessibility/accessibility.css";
import spacingStyles from "@patternfly/patternfly/utilities/Spacing/spacing.css";
import { css } from "@patternfly/react-styles";
import { BellIcon, CogIcon } from "@patternfly/react-icons";
import ConnectForm from "./connect-form";
import ConnectPage from "./connectPage";
import DashboardPage from "./overview/dashboard/dashboardPage";
import OverviewTablePage from "./overview/overviewTablePage";
import TopologyPage from "./topology/qdrTopology";
import MessageFlowPage from "./chord/qdrChord";
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
      activeItem: "dashboard"
    };
    this.tables = ["routers", "addresses", "links", "connections", "logs"];

    /*
      connections: [
        { title: "name", displayName: "host" },
        { title: "container" },
        { title: "role" },
        { title: "dir" },
        { title: "security" },
        { title: "authentication" },
        { title: "close" }
      ],
      logs: [
        { title: "Module" },
        { title: "Notice" },
        { title: "Info" },
        { title: "Trace" },
        { title: "Debug" },
        { title: "Warning" },
        { title: "Error" },
        { title: "Critical" }
      ]
      */
    this.hooks = { setLocation: this.setLocation };
    this.service = new QDRService(this.hooks);
  }

  setLocation = where => {
    //console.log(`setLocation to ${where}`);
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
  icap = s => s.charAt(0).toUpperCase() + s.slice(1);

  render() {
    const {
      isDropdownOpen,
      isKebabDropdownOpen,
      activeItem,
      activeGroup
    } = this.state;

    const PageNav = (
      <Nav onSelect={this.onNavSelect} aria-label="Nav" className="pf-m-dark">
        <NavList>
          <NavExpandable
            title="Overview"
            groupId="overview"
            isActive={activeGroup === "overview"}
            isExpanded
          >
            <NavItem
              groupId="overview"
              itemId="dashboard"
              isActive={activeItem === "dashboard"}
            >
              Dashboard
            </NavItem>
            {this.tables.map(t => {
              return (
                <NavItem
                  groupId="overview"
                  itemId={t}
                  isActive={activeItem === { t }}
                  key={t}
                >
                  {this.icap(t)}
                </NavItem>
              );
            })}
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
        logo={<span className="logo-text">Apache Qpid Dispatch Console</span>}
        toolbar={PageToolbar}
        avatar={<Avatar src={avatarImg} alt="Avatar image" />}
        showNavToggle
      />
    );
    const Sidebar = <PageSidebar nav={PageNav} className="pf-m-dark" />;
    const pageId = "main-content-page-layout-expandable-nav";
    const PageSkipToContent = (
      <SkipToContent href={`#${pageId}`}>Skip to Content</SkipToContent>
    );
    const activeItemToPage = () => {
      if (this.state.activeGroup === "overview") {
        if (this.state.activeItem === "dashboard") {
          return <DashboardPage service={this.service} />;
        }
        return (
          <OverviewTablePage
            entity={this.state.activeItem}
            service={this.service}
          />
        );
      } else if (this.state.activeGroup === "visualizations") {
        if (this.state.activeItem === "topology") {
          return <TopologyPage service={this.service} />;
        } else {
          return <MessageFlowPage service={this.service} />;
        }
      }
      //console.log("using overview charts page");
      return <DashboardPage service={this.service} />;
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
