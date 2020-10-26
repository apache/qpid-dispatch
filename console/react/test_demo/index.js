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
import ReactDOM from "react-dom";
import '@patternfly/react-core/dist/styles/base.css';
import "@patternfly/patternfly/patternfly-addons.css";
import "../src/patternfly.css";
import "font-awesome/css/font-awesome.min.css";
import "@patternfly/patternfly/components/Nav/nav.css";
import "../src/App.css";

import { HashRouter as Router, Route } from "react-router-dom";
import { PageSection, PageSectionVariants } from "@patternfly/react-core";
import { QDRService } from "../src/common/qdrService";
import { mockService } from "../test_data/qdrService.mock";
import TopologyViewer from "../src/topology/topologyViewer";
import PageLayout from "../src/overview/dashboard/layout";
// import * as world from "src/public/data/countries.json";


let config = {title: "Apache Qpid Dispatch Console"};
fetch("/config.json")
    .then(res => res.json())
    .then(cfg => {
        config = cfg;
    })
    .catch(error => {
        console.log("/config.json not found. Using default console title");
    })
    .finally(() => {
            // ReactDOM.render(<App config={config} />, document.getElementById("root"))


            // jest.spyOn(window, "fetch").mockImplementationOnce(() => {
            //     return Promise.resolve({
            //         json: () => Promise.resolve(world)
            //     });
            // });
            // let service = new QDRService()
            let service = new mockService({onSendMethod: null})
            const props = {
                service,
                handleAddNotification: () => {
                }
            };
            // <div id='topology'>
            // ReactDOM.render(<TopologyViewer {...props} />, document.getElementById("root"));
            // ReactDOM.render(<TopologyViewer {...props} />, document.getElementById("root"));
        ReactDOM.render(
        // <PageSection
        //     data-testid="topology-page"
        //     variant={PageSectionVariants.light}
        //     id="topologyPage"
        // >
        //     <TopologyViewer
        //         {...props}
        //     />
        // </PageSection>
            <div className="App pf-m-redhat-font">
                <div className="pf-c-page">
            <main id="main-content-page-layout-manual-nav" className="pf-c-page__main" tabIndex="-1"
                  style={{"backgroundColor": "rgb(255, 255, 255)"}}>
                {/*<section data-testid="topology-page" id="topologyPage" className="pf-c-page__main-section pf-m-light">*/}
                       <PageSection
                            data-testid="topology-page"
                            variant={PageSectionVariants.light}
                            id="topologyPage"
                        >
                            <TopologyViewer
                                {...props}
                            />
                        </PageSection>
                {/*</section>*/}
            </main>
            </div>
            </div>

            , document.getElementById("root"));
        }
    );


// login(async () => {
//     const { getByLabelText, getByText, getByTestId } = render(
//         <TopologyViewer {...props} />
//     );
//
//     // make sure it rendered the component
//     const pfTopologyView = getByLabelText("topology-viewer");
//     expect(pfTopologyView).toBeInTheDocument();
//     expect(getByLabelText("topology-diagram")).toBeInTheDocument();
//
//     // make sure it created the svg
//     await waitForElement(() => getByLabelText("topology-svg"));
//
//     // the svg should have a router circle
//     await waitForElement(() => getByTestId("router-0"));
//
//     // "click" on the router
//     const router = getByTestId("router-0");
//     fireEvent.mouseDown(router);
//     fireEvent.mouseUp(router);
//
//     // the routerInfo modal appears
//     await waitForElement(() => getByLabelText("Close"));
//     // close the modal
//     fireEvent.click(getByLabelText("Close"));
//
//     // the svg should have a client circle
//     const client = getByTestId("client-2");
//     // "click" on the client
//     fireEvent.mouseDown(client);
//     fireEvent.mouseUp(client);
//
//     // the clientInfo modal appears
//     await waitForElement(() => getByLabelText("Close"));
//     // close the modal
//     fireEvent.click(getByLabelText("Close"));
//
//     // make sure the legend opens
//     const legendButton = getByText("topology-legend");
//     expect(legendButton).toBeInTheDocument();
//     fireEvent.click(legendButton);
//     fireEvent.click(getByLabelText("Close"));
//
//     // turn on the traffic animation
//
//     // dropdown the traffic panel
//     fireEvent.contextMenu(client);
//     fireEvent.click(pfTopologyView);
//     const trafficButton = getByLabelText("button-for-Traffic");
//     expect(trafficButton).toBeInTheDocument();
//     fireEvent.click(trafficButton);
//
//     // click on the show traffic checkbox
//     const trafficCheckbox = getByLabelText("show traffic by address");
//     fireEvent.click(trafficCheckbox);
//     // the address dot should be there
//     await waitForElement(() => getByText("toB"));
// });
