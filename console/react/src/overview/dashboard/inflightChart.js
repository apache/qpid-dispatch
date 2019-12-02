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

import { ChartThemeColor } from "@patternfly/react-charts";
import ChartBase from "./chartBase";
import * as d3 from "d3";

class InflightChart extends ChartBase {
  constructor(props) {
    super(props);
    this.title = "Messages in flight";
    this.color = d3.rgb(ChartThemeColor.green);
    this.setStyle(this.color, 0.3);
    this.isRate = false;
    this.ariaLabel = "inflight-chart";
  }

  updateData = () => {
    this.props.service.management.topology.fetchAllEntities(
      {
        entity: "router.link",
        attrs: ["undeliveredCount", "unsettledCount", "linkType", "linkDir"]
      },
      results => {
        let inflight = 0;
        for (let id in results) {
          const aresult = results[id]["router.link"];
          for (let i = 0; i < aresult.results.length; i++) {
            const result = this.props.service.utilities.flatten(
              aresult.attributeNames,
              aresult.results[i]
            );
            inflight +=
              result.linkType === "endpoint" && result.linkDir === "out"
                ? parseInt(result.unsettledCount) + parseInt(result.undeliveredCount)
                : 0;
          }
        }
        this.addData(inflight);
      }
    );
  };
}

export default InflightChart;
