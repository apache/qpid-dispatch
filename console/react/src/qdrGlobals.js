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

import config from "./config.json";
/* globals Promise */
export var QDRFolder = (function() {
  function Folder(title) {
    this.title = title;
    this.children = [];
    this.folder = true;
  }
  return Folder;
})();
export var QDRLeaf = (function() {
  function Leaf(title) {
    this.title = title;
  }
  return Leaf;
})();

export class QDRLogger {
  constructor(log, source) {
    this.log = function(msg) {
      log.log(
        " % c % s % s % s",
        "color: yellow; background - color: black;",
        "QDR-",
        source,
        msg
      );
    };
    this.debug = this.log;
    this.error = this.log;
    this.info = this.log;
    this.warn = this.log;
  }
}

export const QDRTemplatePath = "html/";
export const QDR_SETTINGS_KEY = "QDRSettings";
export const QDR_LAST_LOCATION = "QDRLastLocation";
export const QDR_INTERVAL = "QDRInterval";

export var getConfigVars = () =>
  new Promise(resolve => {
    const s = {};
    s.QDR_CONSOLE_TITLE = config.title;
    document.title = s.QDR_CONSOLE_TITLE;
    resolve(s);
  });
