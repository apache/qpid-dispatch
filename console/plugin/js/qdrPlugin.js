/**
 * @module QDR
 * @main QDR
 *
 * The main entrypoint for the QDR module
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

  /**
   * @property log
   * @type {Logging.Logger}
   *
   * This plugin's logger instance
   */
  QDR.log = Logger.get(QDR.pluginName);

  /**
   * @property templatePath
   * @type {string}
   *
   * The top level path to this plugin's partials
   */
  QDR.srcBase = "../dispatch/plugin/";
  QDR.templatePath = QDR.srcBase + "html/";
  QDR.cssPath = QDR.srcBase + "css/";

  /**
   * @property jmxDomain
   * @type {string}
   *
   * The JMX domain this plugin mostly works with
   */
  QDR.jmxDomain = "hawtio"

  /**
   * @property mbeanType
   * @type {string}
   *
   * The mbean type this plugin will work with
   */
  QDR.mbeanType = "IRCHandler";

  /**
   * @property mbean
   * @type {string}
   *
   * The mbean's full object name
   */
  QDR.mbean = QDR.jmxDomain + ":type=" + QDR.mbeanType;

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
  QDR.module = angular.module(QDR.pluginName, ['hawtioCore', 'hawtio-ui', 'hawtio-forms'/*, 'luegg.directives'*/]);

  // set up the routing for this plugin
  QDR.module.config(function($routeProvider) {
    $routeProvider
      .when('/', {
        templateUrl: QDR.templatePath + 'qdrConnect.html'
        })
        .when('/overview', {
          templateUrl: QDR.templatePath + 'qdrOverview.html'
        })
        .when('/topology', {
          templateUrl: QDR.templatePath + 'qdrTopology.html'
        })
        .when('/list', {
          templateUrl: QDR.templatePath + 'qdrList.html'
        })
        .when('/schema', {
          templateUrl: QDR.templatePath + 'qdrSchema.html'
        })
        .when('/charts', {
          templateUrl: QDR.templatePath + 'qdrCharts.html'
        })
        .when('/connect', {
          templateUrl: QDR.templatePath + 'qdrConnect.html'
        });
  });

  QDR.module.config(['$compileProvider', function ($compileProvider) {
	var cur = $compileProvider.urlSanitizationWhitelist();
    $compileProvider.urlSanitizationWhitelist(/^\s*(https?|ftp|mailto|file|blob):/);
	cur = $compileProvider.urlSanitizationWhitelist();
  }]);

    // one-time initialization happens in the run function
  // of our module
  QDR.module.run(function(workspace, viewRegistry, localStorage, QDRService, QDRChartService, dialogService, $rootScope, $location) {
    // let folks know we're actually running
    QDR.log.info("plugin running");

    Core.addCSS(QDR.cssPath + 'plugin.css');
    Core.addCSS(QDR.cssPath + 'qdrTopology.css');
    Core.addCSS(QDR.cssPath + 'json-formatter-min.css');
    Core.addCSS(QDR.cssPath + 'jquery-minicolors.css');


    // tell hawtio that we have our own custom layout for
    // our view
    viewRegistry["dispatch"] = QDR.templatePath + "qdrLayout.html";

    // Add a top level tab to hawtio's navigation bar
    workspace.topLevelTabs.push({
      id: "irc",
      content: "Qpid Dispatch Router",
      title: "example QDR client",
      isValid: function(workspace) { return true; },
      href: function() { return "#/connect"; },
      isActive: function() { return workspace.isLinkActive("irc"); }
    });
    
    QDRService.initProton();
    var settings = angular.fromJson(localStorage[QDR.SETTINGS_KEY]);
    var lastLocation = localStorage[QDR.LAST_LOCATION];
    if (!angular.isDefined(lastLocation))
        lastLocation = "/overview";
    QDRService.addConnectAction(function() {
      QDRChartService.init(); // initialize charting service after we are connected
    });
    if (settings && settings.autostart) {
      QDR.log.debug("Settings.autostart set, starting QDR connection");
      QDRService.addConnectAction(function() {
        Core.notification('info', "Connected to QDR Server");
        $location.path(lastLocation);
        Core.$apply($rootScope);
      });
      QDRService.connect(settings);
    }

    $rootScope.$on('$routeChangeSuccess', function() {
        localStorage[QDR.LAST_LOCATION] = $location.$$path;

        console.log("should save " + $location.$$path);
    });


  });

  return QDR;
}(QDR || {}));

// Very important!  Add our module to hawtioPluginLoader so it
// bootstraps our module
hawtioPluginLoader.addModule(QDR.pluginName);

// have to add this third-party directive too
//hawtioPluginLoader.addModule('luegg.directives');
hawtioPluginLoader.addModule('jsonFormatter');
hawtioPluginLoader.addModule('ngGrid');
hawtioPluginLoader.addModule('dialogService');
hawtioPluginLoader.addModule('ui.slider');
hawtioPluginLoader.addModule('minicolors');
