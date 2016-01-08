/**
 * @module QDR
 */
var QDR = (function (QDR) {

  /**
   * @property breadcrumbs
   * @type {{content: string, title: string, isValid: isValid, href: string}[]}
   *
   * Data structure that defines the sub-level tabs for
   * our plugin, used by the navbar controller to show
   * or hide tabs based on some criteria
   */
  QDR.breadcrumbs = [
    {
        content: '<i class="icon-cogs"></i> Connect',
        title: "Connect to a router",
        isValid: function (QDRService) { return true; },
        href: "#/connect"
    },
    {
        content: '<i class="fa fa-home"></i> Overview',
        title: "View router overview",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#/overview"
      },
    {
        content: '<i class="icon-star-empty"></i> Topology',
        title: "View router network topology",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#/topology"
      },
    {
        content: '<i class="icon-list "></i> List',
        title: "View router nodes as a list",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#/list"
      },
    {
        content: '<i class="icon-bar-chart"></i> Charts',
        title: "View charts",
        isValid: function (QDRService, $location) { return QDRService.isConnected(); },
        href: "#/charts"
    },
    {
        content: '<i class="icon-align-left"></i> Schema',
        title: "View dispatch schema",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#/schema",
        right: true

      }
  ];
  /**
   * @function NavBarController
   *
   * @param $scope
   * @param workspace
   *
   * The controller for this plugin's navigation bar
   *
   */
  QDR.NavBarController = function($scope, QDRService, QDRChartService, $location) {

    if (($location.path().startsWith("/main") || $location.path().startsWith("/topology") )
    && !QDRService.isConnected()) {
      $location.path("/connect");
    }

    if (($location.path().startsWith("/main") || $location.path().startsWith("/connect") )
    && QDRService.isConnected()) {
      $location.path("/topology");
    }

    $scope.breadcrumbs = QDR.breadcrumbs;

    $scope.isValid = function(link) {
      return link.isValid(QDRService, $location);
    };

    $scope.isActive = function(href) {
        return href.split("#")[1] == $location.path();
    };

    $scope.isRight = function (link) {
        return angular.isDefined(link.right);
    };

    $scope.hasChart = function (link) {
        if (link.href == "#/charts") {
            return QDRChartService.charts.some(function (c) { return c.dashboard });
        }

    }
  };

  return QDR;

} (QDR || {}));
