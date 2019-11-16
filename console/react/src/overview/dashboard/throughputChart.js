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

import ChartBase from "./chartBase";

class ThroughputChart extends ChartBase {
  constructor(props) {
    super(props);
    this.title = "Deliveries per sec";
    this.color = "#99C2EB"; //ChartThemeColor.blue;
    this.setStyle(this.color);
    this.isRate = true;
    this.ariaLabel = "throughput-chart";
  }

  updateData = () => {
    this.props.service.management.topology.fetchAllEntities(
      {
        entity: "router",
        attrs: ["deliveriesEgress"]
      },
      results => {
        let deliveries = 0;
        for (let id in results) {
          const aresult = results[id]["router"];
          deliveries += parseInt(aresult.results[0]);
        }
        this.addData(deliveries);
      }
    );
  };
}

export default ThroughputChart;
