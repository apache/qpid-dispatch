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

class ChartData {
  constructor(service) {
    this.service = service;
    this.reset();
    this.isRate = false;
  }

  init = datum => {
    for (let i = 0; i < 60 * 60; i++) {
      this.rawData.push(datum);
    }
    this.initialized = true;
  };

  reset = () => {
    this.rates = [];
    this.rawData = [];
    this.rateStorage = {};
    this.initialized = false;
    for (let i = 0; i < 60 * 60; i++) {
      this.rates.push(0);
    }
  };

  addData = datum => {
    if (!this.initialized) {
      this.init(datum);
    }
    this.rawData.push(datum);
    this.rawData.splice(0, 1);
    if (this.isRate) {
      // get the average rate of change for the last three values
      const avg = this.service.utilities.rates(
        { val: datum },
        ["val"],
        this.rateStorage,
        "val",
        3
      );
      datum = Math.round(avg.val);
    }
    if (datum < 0) datum = 0;
    this.rates.push(datum);
    this.rates.splice(0, 1);
  };

  data = period => this.rates.slice(-period);
}

export default ChartData;
