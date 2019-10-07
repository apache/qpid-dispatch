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

import {
  BrowserRouter as Router,
  Switch,
  Route,
  Link,
  Redirect
} from "react-router-dom";

import accessibleStyles from "@patternfly/patternfly/utilities/Accessibility/accessibility.css";
import spacingStyles from "@patternfly/patternfly/utilities/Spacing/spacing.css";
import { css } from "@patternfly/react-styles";
import { BellIcon, CogIcon, PowerOffIcon } from "@patternfly/react-icons";
//import ConnectForm from "./connect-form";
import ConnectPage from "./connectPage";
import DashboardPage from "./overview/dashboard/dashboardPage";
import OverviewTablePage from "./overview/overviewTablePage";
import DetailsTablePage from "./overview/detailsTablePage";
import TopologyPage from "./topology/qdrTopology";
import MessageFlowPage from "./chord/qdrChord";
import { QDRService } from "./qdrService";
const avatarImg = require("./assets/img_avatar.svg");

class PageLayout extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      connected: false,
      connectPath: "",
      isDropdownOpen: false,
      activeGroup: "overview",
      activeItem: "dashboard",
      detailInfo: null,
      detailMeta: null
    };
    this.tables = ["routers", "addresses", "links", "connections", "logs"];

    /*
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
    //this.setState({ connectPath: where })
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

  handleConnect = connectPath => {
    this.service
      .connect({ address: "localhost", port: 5673, reconnect: true })
      .then(
        r => {
          this.setState({
            connected: true,
            connectPath
          });
        },
        e => {
          console.log(e);
        }
      );
  };

  handleConnectCancel = () => {};
  onNavSelect = result => {
    this.setState({
      activeItem: result.itemId,
      activeGroup: result.groupId,
      connectPath: ""
    });
  };
  icap = s => s.charAt(0).toUpperCase() + s.slice(1);

  showDetailTable = (_value, detailInfo, activeItem, detailMeta) => {
    this.setState({
      activeGroup: "detailsTable",
      activeItem,
      detailInfo,
      detailMeta,
      connectPath: "/details"
    });
  };

  BreadcrumbSelected = connectPath => {
    this.setState({
      connectPath
    });
  };

  toggleConnectForm = event => {
    console.log("taggleConnectForm called with event.target");
    console.log(event.target);
  };

  render() {
    const { isDropdownOpen, activeItem, activeGroup } = this.state;

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
              <Link to="/dashboard">Dashboard</Link>
            </NavItem>
            {this.tables.map(t => {
              return (
                <NavItem
                  groupId="overview"
                  itemId={t}
                  isActive={activeItem === { t }}
                  key={t}
                >
                  <Link to={`/overview/${t}`}>{this.icap(t)}</Link>
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
              <Link to="/topology">Topology</Link>
            </NavItem>
            <NavItem
              groupId="visualizations"
              itemId="flow"
              isActive={activeItem === "flow"}
            >
              <Link to="/flow">Message flow</Link>
            </NavItem>
          </NavExpandable>
          <NavExpandable
            title="Details"
            groupId="detailsGroup"
            isActive={activeGroup === "detailsGroup"}
          >
            <NavItem
              groupId="detailsGroup"
              itemId="entities"
              isActive={activeItem === "entities"}
            >
              <Link to="/entities">Entities</Link>
            </NavItem>
            <NavItem
              groupId="detailsGroup"
              itemId="schema"
              isActive={activeItem === "schema"}
            >
              <Link to="/schema">Schema</Link>
            </NavItem>
          </NavExpandable>
        </NavList>
      </Nav>
    );
    const userDropdownItems = [
      <DropdownItem component="button" key="action">
        Logout
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
            <Button
              id="connectButton"
              onClick={this.toggleConnectForm}
              aria-label="Toggle Connect Form"
              variant={ButtonVariant.plain}
            >
              <PowerOffIcon />
            </Button>
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
        </ToolbarGroup>
        <ToolbarGroup>
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
    const pageId = "main-content-page-layout-expandable-nav";
    const PageSkipToContent = (
      <SkipToContent href={`#${pageId}`}>Skip to Content</SkipToContent>
    );

    const sidebar = PageNav => {
      if (this.state.connected) {
        return <PageSidebar nav={PageNav} className="pf-m-dark" />;
      }
      return <React.Fragment />;
    };

    // don't allow access to this component unless we are logged in
    const PrivateRoute = ({ component: Component, path: rpath, ...more }) => (
      <Route
        path={rpath}
        {...(more.exact ? "exact" : "")}
        render={props =>
          this.state.connected ? (
            <Component service={this.service} {...props} {...more} />
          ) : (
            <Redirect
              to={{ pathname: "/login", state: { from: props.location } }}
            />
          )
        }
      />
    );

    // When we need to display a different component(page),
    // we render a <Redirect> object
    const redirectAfterConnect = () => {
      let { connectPath } = this.state;
      if (connectPath !== "") {
        if (connectPath === "/login") connectPath = "/";
        return <Redirect to={connectPath} />;
      }
      return <React.Fragment />;
    };

    return (
      <Router>
        {redirectAfterConnect()}
        <Page
          header={Header}
          sidebar={sidebar(PageNav)}
          isManagedSidebar
          skipToContent={PageSkipToContent}
        >
          <Switch>
            <PrivateRoute path="/" exact component={DashboardPage} />
            <PrivateRoute path="/dashboard" exact component={DashboardPage} />
            <PrivateRoute
              path="/overview/:entity"
              component={OverviewTablePage}
            />
            <PrivateRoute path="/details" component={DetailsTablePage} />
            <PrivateRoute path="/topology" component={TopologyPage} />
            <PrivateRoute path="/flow" component={MessageFlowPage} />
            <Route
              path="/login"
              render={props => (
                <ConnectPage {...props} handleConnect={this.handleConnect} />
              )}
            />
          </Switch>
        </Page>
      </Router>
    );
  }
}

export default PageLayout;

/*          <ToolbarItem>
            <ConnectForm prefix="toolbar" handleConnect={this.handleConnect} />
          </ToolbarItem>

          */
