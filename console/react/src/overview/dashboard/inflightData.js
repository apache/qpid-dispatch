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

import ChartData from "./chartData";

class InflightData extends ChartData {
  constructor(service) {
    super(service);
    this.isRate = false;
    this.running = true;
  }

  stop = () => {
    this.running = false;
  };
  updateData = () => {
    if (!this.service.management.connection.is_connected()) return;
    this.service.management.topology.fetchAllEntities(
      {
        entity: "router.link",
        attrs: ["undeliveredCount", "unsettledCount", "linkType", "linkDir"]
      },
      results => {
        if (!this.running) return;
        let inflight = 0;
        for (let id in results) {
          const aresult = results[id]["router.link"];
          for (let i = 0; i < aresult.results.length; i++) {
            const result = this.service.utilities.flatten(
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

export default InflightData;
