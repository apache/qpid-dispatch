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

export var QDRCore = {
  notification: function(severity, msg) {
    $.notify(msg, severity);
  }
};

export class QDRLogger {
  constructor($log, source) {
    this.log = function(msg) {
      $log.log(`QDR-${source}: ${msg}`);
    };
    this.debug = function(msg) {
      $log.debug(`QDR-${source}: ${msg}`);
    };
    this.error = function(msg) {
      $log.error(`QDR-${source}: ${msg}`);
    };
    this.info = function(msg) {
      $log.info(`QDR-${source}: ${msg}`);
    };
    this.warn = function(msg) {
      $log.warn(`QDR-${source}: ${msg}`);
    };
  }
}

export const QDRTemplatePath = "html/";
export const QDR_SETTINGS_KEY = "QDRSettings";
export const QDR_LAST_LOCATION = "QDRLastLocation";
export const QDR_INTERVAL = "QDRInterval";

export var QDRRedirectWhenConnected = function($location, org) {
  $location.path("/connect");
  $location.search("org", org);
};

export var getConfigVars = () =>
  new Promise(resolve => {
    $.getJSON("config.json", function() {}).done(function(s) {
      s.QDR_CONSOLE_TITLE = s.title;
      document.title = s.QDR_CONSOLE_TITLE;
      resolve(s);
    });
  });

$(document).ready(getConfigVars);
