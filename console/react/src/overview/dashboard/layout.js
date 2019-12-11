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
  DropdownToggle,
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

import { HashRouter as Router, Switch, Route, Link, Redirect } from "react-router-dom";

import accessibleStyles from "@patternfly/patternfly/utilities/Accessibility/accessibility.css";
import { css } from "@patternfly/react-styles";
import { PowerOffIcon } from "@patternfly/react-icons";
import DropdownMenu from "../../common/DropdownMenu";
import ConnectPage from "../../connect/connectPage";
import DashboardPage from "./dashboardPage";
import OverviewPage from "../overviewPage";
import DetailsTablePage from "../../details/detailsTablePage";
import EntitiesPage from "../../details/entitiesPage";
import TopologyPage from "../../topology/topologyPage";
import MessageFlowPage from "../../chord/chordPage";
import SchemaPage from "../../details/schema/schemaPage";
import LogDetails from "../logDetails";
import ConnectForm from "../../connect/connect-form";
import NotificationDrawer from "./notificationDrawer";
import { utils } from "../../common/amqp/utilities";
import throughputData from "./throughputData";
import inflightData from "./inflightData";

class PageLayout extends React.PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      connected: false,
      connecting: false,
      isConnectFormOpen: false,
      activeGroup: "overview",
      activeItem: "dashboard",
      isNavOpenDesktop: true,
      isNavOpenMobile: false,
      isMobileView: false,
      user: "anonymous",
      timePeriod: 60
    };
    this.isDropdownOpen = false;

    this.service = this.props.service;
    this.service.setHooks({ setLocation: this.setLocation });
    this.nav = {
      overview: [
        { name: "dashboard" },
        { name: "routers", pre: true },
        { name: "addresses", pre: true },
        { name: "links", pre: true },
        { name: "connections", pre: true },
        { name: "logs", pre: true }
      ],
      visualizations: [{ name: "topology" }, { name: "flow", title: "Message flow" }],
      details: [{ name: "entities" }, { name: "schema" }]
    };
    //this.state.connecting = true;
    //this.tryInitialConnect();
  }

  componentDidMount = () => {
    this.chartTimer = setInterval(this.updateCharts, 1000);
    this.throughputChartData = new throughputData(this.service);
    this.inflightChartData = new inflightData(this.service);
    this.updateCharts();
  };

  componentWillUnmount = () => {
    if (this.chartTimer) {
      this.throughputChartData.stop();
      this.inflightChartData.stop();
      clearInterval(this.chartTimer);
    }
  };

  tryInitialConnect = () => {
    const connectOptions = {
      address: window.location.hostname,
      port: window.location.port,
      timeout: 2000,
      reconnect: false
    };
    this.service.connect(connectOptions).then(
      () => {
        this.service.setReconnect(true);
        this.schema = this.service.schema;
        this.props.history.replace("/dashboard");
        this.redirect = true;
        this.setState({ connecting: false, connected: true });
      },
      () => {
        //this.service.disconnect();
        this.props.history.replace("/");
        this.setState({ connecting: false });
      }
    );
  };

  // the connection to the routers was lost
  setLocation = whatHappened => {
    if (whatHappened === "disconnect") {
      this.handleAddNotification(
        "event",
        "Connection to router dropped",
        new Date(),
        "warning"
      );
      this.lastLocation = this.props.location.pathname;
      this.setState({ connected: false });
    } else if (whatHappened === "reconnect") {
      this.throughputChartData.reset();
      this.handleAddNotification(
        "event",
        "Connection to router resumed",
        new Date(),
        "info"
      );
      this.redirect = true;
      let to = "/dashboard";
      if (this.lastLocation) {
        to = this.lastLocation;
      }
      this.props.history.push(to);
      this.setState({
        isConnectFormOpen: false,
        connected: true
      });
    }
  };

  updateCharts = () => {
    this.throughputChartData.updateData();
    this.inflightChartData.updateData();
  };

  onDropdownToggle = () => {
    this.isDropdownOpen = !this.isDropdownOpen;
    this.dropdownRef.show(this.isDropdownOpen);
  };

  handleDropdownLogout = () => {
    // called from the user dropdown menu
    // The only menu item is logout
    // We must have been connected to get here
    this.handleConnect();
    this.dropdownRef.show(false);
    this.isDropdownOpen = false;
  };

  handleConnect = (connectPath, result) => {
    if (this.state.connected) {
      this.setState({ connected: false }, () => {
        this.handleConnectCancel();
        this.service.disconnect();
        this.handleAddNotification("event", "Manually disconnected", new Date(), "info");
      });
    } else {
      this.schema = this.service.schema;
      if (connectPath === "/") connectPath = "/dashboard";
      const activeItem = connectPath.split("/").pop();
      // find the active group for this item
      let activeGroup = "overview";
      for (const group in this.nav) {
        if (this.nav[group].some(item => item.name === activeItem)) {
          activeGroup = group;
          break;
        }
      }
      this.handleAddNotification(
        "event",
        `Console connected to router`,
        new Date(),
        "success",
        true
      );

      //this.redirect = true;
      this.props.history.replace(connectPath);
      this.setState({
        activeItem,
        activeGroup,
        connected: true,
        isConnectFormOpen: false
      });
    }
  };

  onNavSelect = result => {
    this.setState({
      activeItem: result.itemId,
      activeGroup: result.groupId
    });
  };

  toggleConnectForm = () => {
    this.setState({ isConnectFormOpen: !this.state.isConnectFormOpen });
  };

  handleConnectCancel = () => {
    this.setState({ isConnectFormOpen: false });
  };

  onNavToggleDesktop = () => {
    this.setState({
      isNavOpenDesktop: !this.state.isNavOpenDesktop
    });
  };

  onNavToggleMobile = () => {
    this.setState({
      isNavOpenMobile: !this.state.isNavOpenMobile
    });
  };

  onPageResize = ({ mobileView, windowSize }) => {
    this.setState({
      isMobileView: mobileView
    });
  };

  handleUserMenuHide = () => {
    this.isDropdownOpen = false;
    this.dropdownRef.show(false);
  };

  isConnected = () => {
    return this.state.connected;
  };

  handleAddNotification = (section, message, timestamp, severity, silent) => {
    if (this.notificationRef) {
      this.notificationRef.addNotification({
        section,
        message,
        timestamp,
        severity,
        silent
      });
    }
  };

  render() {
    const { activeItem, activeGroup } = this.state;
    const { isNavOpenDesktop, isNavOpenMobile, isMobileView } = this.state;

    const PageNav = (
      <Nav onSelect={this.onNavSelect} aria-label="Nav" className="pf-m-dark">
        <NavList>
          {Object.keys(this.nav).map(section => {
            const Section = utils.Icap(section);
            return (
              <NavExpandable
                title={Section}
                groupId={section}
                isActive={activeGroup === section}
                isExpanded
                key={section}
              >
                {this.nav[section].map(item => {
                  const key = item.name;
                  return (
                    <NavItem
                      groupId={section}
                      itemId={key}
                      isActive={activeItem === key}
                      key={key}
                    >
                      <Link to={`/${item.pre ? section + "/" : ""}${key}`}>
                        {item.title ? item.title : utils.Icap(key)}
                      </Link>
                    </NavItem>
                  );
                })}
              </NavExpandable>
            );
          })}
        </NavList>
      </Nav>
    );
    const PageToolbar = (
      <Toolbar>
        <ToolbarGroup
          className={css(accessibleStyles.screenReader, accessibleStyles.visibleOnLg)}
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
          <ToolbarItem className="notification-button">
            <NotificationDrawer ref={el => (this.notificationRef = el)} />
          </ToolbarItem>
        </ToolbarGroup>
        <ToolbarGroup>
          <ToolbarItem
            className={css(accessibleStyles.screenReader, accessibleStyles.visibleOnMd)}
          >
            <DropdownToggle className="user-button" onToggle={this.onDropdownToggle}>
              {this.state.user}
            </DropdownToggle>
            <DropdownMenu
              ref={el => (this.dropdownRef = el)}
              handleContextHide={this.handleUserMenuHide}
              handleDropdownLogout={this.handleDropdownLogout}
              isConnected={this.isConnected}
              parentClass="user-button"
            />
          </ToolbarItem>
        </ToolbarGroup>
      </Toolbar>
    );

    const Header = (
      <PageHeader
        className="topology-header"
        logo={<span className="logo-text">{this.props.config.title}</span>}
        toolbar={PageToolbar}
        avatar={<Avatar src="./assets/img_avatar.svg" alt="Avatar image" />}
        showNavToggle
        onNavToggle={isMobileView ? this.onNavToggleMobile : this.onNavToggleDesktop}
        isNavOpen={isMobileView ? isNavOpenMobile : isNavOpenDesktop}
      />
    );
    const pageId = "main-content-page-layout-manual-nav";
    const PageSkipToContent = (
      <SkipToContent href={`#${pageId}`}>Skip to Content</SkipToContent>
    );

    const sidebar = PageNav => {
      if (this.state.connected) {
        return (
          <PageSidebar
            id="page-sidebar"
            nav={PageNav}
            isNavOpen={isMobileView ? isNavOpenMobile : isNavOpenDesktop}
            theme="dark"
          />
        );
      }
      // this is required to prevent an axe error
      return <div id="page-sidebar" />;
    };

    // don't allow access to this component unless we are logged in
    const PrivateRoute = ({ component: Component, path: rpath, ...more }) => (
      <Route
        path={rpath}
        {...(more.exact ? "exact" : "")}
        render={props =>
          this.state.connected ? (
            <Component
              service={this.service}
              handleAddNotification={this.handleAddNotification}
              {...props}
              {...more}
            />
          ) : (
            <Redirect
              to={{
                pathname: `/login${this.state.connecting ? "/connecting" : ""}`,
                state: { from: props.location }
              }}
            />
          )
        }
      />
    );

    const connectForm = () => {
      return this.state.isConnectFormOpen ? (
        <ConnectForm
          service={this.service}
          isConnectFormOpen={this.state.isConnectFormOpen}
          fromPath={"/"}
          handleConnect={this.handleConnect}
          handleConnectCancel={this.handleConnectCancel}
          isConnected={this.state.connected}
          fromLayout={true}
        />
      ) : (
        <React.Fragment />
      );
    };

    // When we need to display a different component(page),
    // we render a <Redirect> object
    const redirectAfterConnect = () => {
      if (this.state.connected && this.redirect) {
        this.redirect = false;
        return <Redirect to={this.props.location.pathname} />;
      } else {
        return <React.Fragment />;
      }
    };

    return (
      <Router>
        {redirectAfterConnect()}
        <Page
          header={Header}
          sidebar={sidebar(PageNav)}
          onPageResize={this.onPageResize}
          skipToContent={PageSkipToContent}
          mainContainerId={pageId}
        >
          {connectForm()}
          <Switch>
            <PrivateRoute
              path="/"
              exact
              throughputChartData={this.throughputChartData}
              inflightChartData={this.inflightChartData}
              component={DashboardPage}
            />
            <PrivateRoute
              path="/dashboard"
              throughputChartData={this.throughputChartData}
              inflightChartData={this.inflightChartData}
              component={DashboardPage}
            />
            <PrivateRoute path="/overview/:entity" component={OverviewPage} />
            <PrivateRoute
              path="/details"
              schema={this.schema}
              component={DetailsTablePage}
            />
            <PrivateRoute path="/topology" component={TopologyPage} />
            <PrivateRoute path="/flow" component={MessageFlowPage} />
            <PrivateRoute path="/logs" component={LogDetails} />
            <PrivateRoute path="/entities" component={EntitiesPage} />
            <PrivateRoute path="/schema" schema={this.schema} component={SchemaPage} />
            <Route
              path="/login"
              render={props => (
                <ConnectPage
                  {...props}
                  connecting={this.state.connecting}
                  connectingTitle={
                    this.state.connecting ? "Attempting to auto connect" : undefined
                  }
                  connectingMessage={
                    this.state.connecting
                      ? `Trying to connect to ${window.location.hostname}:${window.location.port}`
                      : undefined
                  }
                  fromPath={"/"}
                  service={this.service}
                  config={this.props.config}
                  handleConnect={this.handleConnect}
                  handleAddNotification={this.handleAddNotification}
                />
              )}
            />
          </Switch>
        </Page>
      </Router>
    );
  }
}

export default PageLayout;
