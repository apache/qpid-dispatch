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
/**
 * @module QDR
 * @main QDR
 *
 * The main entry point for the QDR module
 *
 */
var QDR = (function(QDR) {

  /**
   * @property pluginName
   * @type {string}
   *
   * The name of this plugin
   */
  QDR.pluginName = "QDR";
  QDR.pluginRoot = "";

  /**
   * @property log
   * @type {Logging.Logger}
   *
   * This plugin's logger instance
   */
  //HIO QDR.log = Logger.get(QDR.pluginName);
  /**
   * @property templatePath
   * @type {string}
   *
   * The top level path to this plugin's partials
   */
  QDR.srcBase = "";
  QDR.templatePath = QDR.srcBase + "html/";
  QDR.cssPath = QDR.srcBase + "css/";
  /**
   * @property SETTINGS_KEY
   * @type {string}
   *
   * The key used to fetch our settings from local storage
   */
  QDR.SETTINGS_KEY = 'QDRSettings';
  QDR.LAST_LOCATION = "QDRLastLocation";

  /**
   * @property module
   * @type {object}
   *
   * This plugin's angularjs module instance
   */
  QDR.module = angular.module(QDR.pluginName, ['ngAnimate', 'ngResource', 'ngSanitize', 'ui.bootstrap']);

  Core = {
    notification: function (severity, msg) {
      $.notify(msg, severity)
    }
  }

  QDR.module.config(function ($compileProvider) {
    $compileProvider.aHrefSanitizationWhitelist(/^\s*(https?|ftp|mailto|chrome-extension|file|blob):/);
    $compileProvider.imgSrcSanitizationWhitelist(/^\s*(https?|ftp|mailto|chrome-extension):/);
  })

  QDR.module.filter('to_trusted', ['$sce', function($sce){
          return function(text) {
              return $sce.trustAsHtml(text);
          };
    }]);

  QDR.module.filter('humanify', function (QDRService) {
    return function (input) {
      return QDRService.humanify(input);
    };
  });

  QDR.module.filter('Pascalcase', function () {
    return function (str) {
      if (!str)
        return "";
      return str.replace(/(\w)(\w*)/g,
      function(g0,g1,g2){return g1.toUpperCase() + g2.toLowerCase();});
    }
  })

    QDR.module.filter('safePlural', function () {
          return function (str) {
        var es = ['x', 'ch', 'ss', 'sh']
        for (var i=0; i<es.length; ++i) {
          if (str.endsWith(es[i]))
            return str + 'es'
        }
        if (str.endsWith('y'))
          return str.substr(0, str.length-2) + 'ies'
        if (str.endsWith('s'))
          return str;
        return str + 's'
          }
  })

  QDR.logger = function ($log) {
    var log = $log;

    this.debug = function (msg) { msg = "QDR: " + msg; log.debug(msg)};
    this.error = function (msg) {msg = "QDR: " + msg; log.error(msg)}
    this.info = function (msg) {msg = "QDR: " + msg; log.info(msg)}
    this.warn = function (msg) {msg = "QDR: " + msg; log.warn(msg)}

    return this;
  }
    // one-time initialization happens in the run function
    // of our module
  QDR.module.run( ["$log", "QDRService", function ($log, QDRService) {
    QDR.log = new QDR.logger($log);
    QDR.log.info("*************creating config editor************");
    QDRService.getSchema(function () {
      QDR.log.debug("got schema")
    })
  }]);

  QDR.module.config(['$qProvider', function ($qProvider) {
      $qProvider.errorOnUnhandledRejections(false);
  }]);

  return QDR;
}(QDR || {}));

var Folder = (function () {
    function Folder(title) {
        this.title = title;
    this.children = [];
    this.folder = true;
    }
    return Folder;
})();
var Leaf = (function () {
    function Leaf(title) {
        this.title = title;
    }
    return Leaf;
})();
