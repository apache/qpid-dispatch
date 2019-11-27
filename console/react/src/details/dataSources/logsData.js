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

import DefaultData from "./defaultData";

class LogsData extends DefaultData {
  constructor(service, schema) {
    super(service, schema);
    this.updateMetaData = {
      module: { readOnly: true }
    };
  }

  validate = record => {
    let validated = true;
    let errorText = "";
    const enables = ["trace", "debug", "info", "notice", "warning", "error", "critical"];

    const enableParts = record.enable.split(",");
    if (enableParts.length === 1 && enableParts[0] === "") {
    } else {
      enableParts.forEach(part => {
        part = part.trim();
        if (part.endsWith("+")) part = part.slice(0, -1);
        if (!enables.includes(part)) {
          errorText = `enable must be one of ${enables.join(", ")}`;
          validated = false;
        }
      });
    }
    return { validated, errorText };
  };
}

export default LogsData;
