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
  Accordion,
  AccordionItem,
  AccordionContent,
  AccordionToggle,
  Button,
  NotificationBadge
} from "@patternfly/react-core";

import {
  AngleDoubleLeftIcon,
  AngleDoubleRightIcon,
  BellIcon,
  TimesIcon
} from "@patternfly/react-icons";
import AlertList from "./alertList";
import { safePlural } from "../../common/qdrGlobals";

class NotificationDrawer extends React.Component {
  constructor(props) {
    super(props);
    this.state = {
      isShown: false, // is the drawer shown
      expanded: false, // is the drawer wide
      isAnyUnread: false,
      accordionSections: {
        action: {
          title: "Management Actions",
          isOpen: false,
          events: []
        },
        event: { title: "Notifications", isOpen: false, events: [] }
      }
    };
    this.severityToIcon = {
      info: { icon: "pficon-info", color: "#313131" },
      error: { icon: "pficon-error-circle-o", color: "red" },
      danger: { icon: "pficon-error-circle-o", color: "red" },
      warning: { icon: "pficon-warning-triangle-o", color: "yellow" },
      success: { icon: "pficon-ok", color: "green" }
    };
  }

  componentDidMount() {
    document.addEventListener("mousedown", this.handleClickOutside);
  }

  componentWillUnmount() {
    document.removeEventListener("mousedown", this.handleClickOutside);
  }

  handleClickOutside = event => {
    if (this.notificationRef && !this.notificationRef.contains(event.target)) {
      // don't close if the click was on the bell icon since
      // that icon's event handler will toggle the drawer
      if (this.buttonRef && this.buttonRef.contains(event.target)) return;
      this.close();
    }
  };

  addNotification = ({ section, message, timestamp, severity, silent }) => {
    const { accordionSections } = this.state;
    const event = { message, timestamp, severity };
    event.date = timestamp.toLocaleDateString(undefined, {
      year: "numeric",
      month: "2-digit",
      day: "2-digit"
    });
    event.time = timestamp.toLocaleTimeString(undefined, {
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit"
    });
    event.isRead = false;
    accordionSections[section].events.unshift(event);
    if (this.alertListRef && !silent && this.props.suppress === false) {
      this.alertListRef.addAlert(severity, message);
    }
    this.setState({
      accordionSections,
      isAnyUnread: true
    });
  };

  close = () => {
    this.setState({ isShown: false });
  };

  toggleDrawer = sectionKey => {
    const { accordionSections } = this.state;
    accordionSections[sectionKey].isOpen = !accordionSections[sectionKey].isOpen;
    this.setState(accordionSections);
  };

  toggleExpand = () => {
    this.setState({ expanded: !this.state.expanded });
  };

  toggle = () => {
    this.setState({ isShown: !this.state.isShown });
  };

  setAnyUnread = accordionSections => {
    let isAnyUnread = false;
    for (let sectionKey in accordionSections) {
      isAnyUnread =
        isAnyUnread || accordionSections[sectionKey].events.some(e => !e.isRead);
    }
    this.setState({ accordionSections, isAnyUnread });
  };

  markAsRead = event => {
    event.isRead = true;
    const { accordionSections } = this.state;
    this.setAnyUnread(accordionSections);
  };

  clearAll = sectionKey => {
    const { accordionSections } = this.state;
    accordionSections[sectionKey].events = [];
    this.setAnyUnread(accordionSections);
  };

  markAllRead = sectionKey => {
    const { accordionSections } = this.state;
    accordionSections[sectionKey].events.forEach(e => (e.isRead = true));
    this.setAnyUnread(accordionSections);
  };

  hasUnread = section => {
    return !section.events.some(e => e.isRead);
  };

  render() {
    const DrawerTitle = (
      <div className="drawer-pf-title">
        <Button variant="plain" aria-label="expand" onClick={this.toggleExpand}>
          {this.state.expanded ? <AngleDoubleRightIcon /> : <AngleDoubleLeftIcon />}
        </Button>
        <h3 className="text-center">Notifications Drawer</h3>
        <Button variant="plain" aria-label="close" onClick={this.close}>
          <TimesIcon />
        </Button>
      </div>
    );

    const severityIcon = event => {
      return (
        <i
          className={`pf pficon ${this.severityToIcon[event.severity].icon}`}
          style={{ color: this.severityToIcon[event.severity].color }}
        ></i>
      );
    };

    return (
      <>
        <div ref={el => (this.buttonRef = el)}>
          <NotificationBadge
            isRead={!this.state.isAnyUnread}
            onClick={this.toggle}
            aria-label="Notifications"
          >
            <BellIcon />
          </NotificationBadge>
        </div>
        {<AlertList ref={el => (this.alertListRef = el)} />}
        {this.state.isShown && (
          <div
            ref={el => (this.notificationRef = el)}
            id="NotificationDrawer"
            className={`drawer-pf drawer-pf-notifications-non-clickable 
            ${this.state.expanded ? "expanded" : "compact"}`}
          >
            {DrawerTitle}
            <Accordion>
              {Object.keys(this.state.accordionSections).map(sectionKey => {
                const section = this.state.accordionSections[sectionKey];
                return (
                  <AccordionItem
                    aria-label="notification-item"
                    key={`${sectionKey}-item`}
                  >
                    <AccordionToggle
                      onClick={() => this.toggleDrawer(sectionKey)}
                      isExpanded={section.isOpen}
                      key={sectionKey}
                      id={sectionKey}
                    >
                      {section.title}
                      <span className="panel-counter">{`${
                        section.events.filter(e => !e.isRead).length
                      } new ${safePlural(
                        section.events.filter(e => !e.isRead).length,
                        "event"
                      )}`}</span>
                    </AccordionToggle>
                    <AccordionContent
                      key={`${sectionKey}-content`}
                      isHidden={!section.isOpen}
                      isFixed
                    >
                      <div
                        className={`panel-body ${
                          section.events.length === 0 ? "hidden" : ""
                        }`}
                      >
                        {section.events.map((event, i) => {
                          return (
                            <div
                              key={`${sectionKey}-event-${i}`}
                              className={`drawer-pf-notification ${
                                event.isRead ? "" : "unread"
                              }`}
                              onClick={() => this.markAsRead(event)}
                            >
                              {severityIcon(event)}
                              <div className="drawer-pf-notification-content">
                                <span className="drawer-pf-notification-message">
                                  {event.message}
                                </span>
                                <div className="drawer-pf-notification-info">
                                  <span className="date">{event.date}</span>
                                  <span className="time">{event.time}</span>
                                </div>
                              </div>
                            </div>
                          );
                        })}
                      </div>
                      <div
                        className={`blank-slate-pf ${
                          section.events.length === 0 ? "" : "hidden"
                        }`}
                      >
                        <div className="blank-slate-pf-icon">
                          <span className="pficon pficon-info"></span>
                        </div>
                        <h5>There are no notifications to display.</h5>
                      </div>
                      <div
                        className={`drawer-pf-action ${
                          section.events.length > 0 ? "" : "hidden"
                        }`}
                      >
                        <div className="drawer-pf-action-link">
                          <button
                            className={`btn btn-link ${
                              this.hasUnread(section) ? "" : "disabled"
                            }`}
                            onClick={() => this.markAllRead(sectionKey)}
                          >
                            Mark All Read
                          </button>
                        </div>
                        <div className="drawer-pf-action-link">
                          <button
                            className="btn btn-link"
                            onClick={() => this.clearAll(sectionKey)}
                          >
                            <span className="pficon pficon-close"></span>
                            Clear All
                          </button>
                        </div>
                      </div>
                    </AccordionContent>
                  </AccordionItem>
                );
              })}
            </Accordion>
          </div>
        )}
      </>
    );
  }
}

export default NotificationDrawer;
