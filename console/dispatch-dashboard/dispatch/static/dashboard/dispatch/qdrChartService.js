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
 */
var QDR = (function(QDR) {
  'use strict';

    // The QDR chart service handles periodic gathering data for charts and displaying the charts
  angular
    .module('horizon.dashboard.dispatch')
    .factory('horizon.dashboard.dispatch.chartService', QDRChartService);

  QDRChartService.$inject = [
    '$rootScope',
    'horizon.dashboard.dispatch.comService',
    '$http',
    '$location'
  ];

  function QDRChartService(
    $rootScope,
    QDRService,
    $http,
    $location) {
      var instance = 0;   // counter for chart instances
      var bases = [];
      var findBase = function (name, attr, request) {
        for (var i=0; i<bases.length; ++i) {
          var base = bases[i];
          if (base.equals(name, attr, request))
            return base;
        }
        return null;
      }

      function ChartBase(name, attr, request) {
        // the base chart attributes
        this.name = name;           // the record's "name" field
        this.attr = attr;           // the record's attr field to chart
        this.request = request;     // the associated request that fetches the data

        // copy the savable properties to an object
        this.copyProps = function (o) {
          o.name = this.name;
          o.attr = this.attr;
          this.request.copyProps(o);
        }

        this.equals = function (name, attr, request) {
          return (this.name == name && this.attr == attr && this.request.equals(request));
        }
      };

      // Object that represents a visible chart
      // There can be multiple of these per ChartBase (eg. one rate  and one value chart)
      function Chart(opts, request) { //name, attr, cinstance, request) {

        var base = findBase(opts.name, opts.attr, request);
        if (!base) {
            base = new ChartBase(opts.name, opts.attr, request);
            bases.push(base);
        }
        this.base = base;
        this.instance = angular.isDefined(opts.instance) ? opts.instance : ++instance;
        this.dashboard = false;     // is this chart on the dashboard page
        this.hdash = false;         // is this chart on the hawtio dashboard page
        this.hreq = false;          // has this hdash chart been requested
        this.type = opts.type ? opts.type: "value";        // value or rate
        this.rateWindow = opts.rateWindow ? opts.rateWindow : 1000;     // calculate the rate of change over this time interval. higher == smother graph
        this.areaColor = "#cbe7f3"; // the chart's area color when not an empty string
        this.lineColor = "#058dc7"; // the chart's line color when not an empty string
        this.visibleDuration = opts.visibleDuration ? opts.visibleDuration : 10;  // number of minutes of data to show (<= base.duration)
        this.userTitle = null;      // user title overrides title()

        // generate a unique id for this chart
        this.id = function () {
          var name = this.name()
          var nameparts = name.split('/');
          if (nameparts.length == 2)
              name = nameparts[1];
          var key = (QDRService.nameFromId(this.request().nodeId) || '') + this.request().entity + name + this.attr() + "_" + this.instance + "_" + (this.request().aggregate ? "1" : "0");
          // remove all characters except letters,numbers, and _
          return key.replace(/[^\w]/gi, '')
        }
        // copy the savable properties to an object
        this.copyProps = function (o) {
          o.type = this.type;
          o.rateWindow = this.rateWindow;
          o.areaColor = this.areaColor;
          o.lineColor = this.lineColor;
          o.visibleDuration = this.visibleDuration;
          o.userTitle = this.userTitle;
          o.dashboard = this.dashboard;
          o.hdash = this.hdash;
          o.instance = this.instance;
          this.base.copyProps(o);
        }
        this.name = function (_) {
          if (!arguments.length) return this.base.name;
          this.base.name = _;
          return this;
        }
        this.attr = function (_) {
          if (!arguments.length) return this.base.attr;
          this.base.attr = _;
          return this;
        }
        this.nodeId = function (_) {
          if (!arguments.length) return this.base.request.nodeId;
          this.base.request.nodeId = _;
          return this;
        }
        this.entity = function (_) {
          if (!arguments.length) return this.base.request.entity;
          this.base.request.entity = _;
          return this;
        }
        this.aggregate = function (_) {
          if (!arguments.length) return this.base.request.aggregate;
          this.base.request.aggregate = _;
          return this;
        }
        this.request = function (_) {
          if (!arguments.length) return this.base.request;
          this.base.request = _;
          return this;
        }
        this.data = function () {
          return this.base.request.data(this.base.name, this.base.attr); // refernce to chart's data array
        }
        this.interval = function (_) {
          if (!arguments.length) return this.base.request.interval;
          this.base.request.interval = _;
          return this;
        }
        this.duration = function (_) {
          if (!arguments.length) return this.base.request.duration;
          this.base.request.duration = _;
          return this;
        }
        this.title = function (_) {
          var name = this.request().aggregate ? 'Aggregate' : (QDRService.nameFromId(this.nodeId()) || this.nodeId());
          var computed = name +
             " " + QDRService.humanify(this.attr()) +
             " - " + this.name()
          if (!arguments.length) return this.userTitle || computed;

          // don't store computed title in userTitle
          if (_ === computed)
            _ = null;
          this.userTitle = _;
          return this;
        }
        this.title_short = function (_) {
          if (!arguments.length) return this.userTitle || this.name();
          return this;
        }
        this.copy = function () {
          var chart = self.registerChart({
            nodeId: this.nodeId(),
            entity: this.entity(),
            name:   this.name(),
            attr:   this.attr(),
            interval: this.interval(),
            forceCreate: true,
            aggregate: this.aggregate(),
            hdash: this.hdash
          })
          chart.type = this.type;
          chart.areaColor = this.areaColor;
          chart.lineColor = this.lineColor;
          chart.rateWindow = this.rateWindow;
          chart.visibleDuration = this.visibleDuration;
          chart.userTitle = this.userTitle;
          return chart;
        }
        // compare to a chart
        this.equals = function (c) {
          return (c.instance == this.instance &&
            c.base.equals(this.base.name, this.base.attr, this.base.request) &&
            c.type == this.type &&
            c.rateWindow == this.rateWindow &&
            c.areaColor == this.areaColor &&
            c.lineColor == this.lineColor)
        }
      }

      // Object that represents the management request to fetch and store data for multiple charts
      function ChartRequest(opts) { //nodeId, entity, name, attr, interval, aggregate) {
        this.duration = opts.duration || 10;    // number of minutes to keep the data
        this.nodeId = opts.nodeId;              // eg amqp:/_topo/0/QDR.A/$management
        this.entity = opts.entity;              // eg .router.address
        // sorted since the responses will always be sorted
        this.aggregate = opts.aggregate;        // list of nodeIds for aggregate charts
        this.datum = {};                        // object containing array of arrays for each attr
                                                // like {attr1: [[date,value],[date,value]...], attr2: [[date,value]...]}

        this.interval = opts.interval || 1000;  // number of milliseconds between updates to data
        this.setTimeoutHandle = null;           // used to cancel the next request
        // copy the savable properties to an object

        this.data = function (name, attr) {
          if (this.datum[name] && this.datum[name][attr])
            return this.datum[name][attr]
          return null;
        }
        this.addAttrName = function (name, attr) {
          if (Object.keys(this.datum).indexOf(name) == -1) {
            this.datum[name] = {}
          }
          if (Object.keys(this.datum[name]).indexOf(attr) == -1) {
            this.datum[name][attr] = [];
          }
        }
        this.addAttrName(opts.name, opts.attr)

        this.copyProps = function (o) {
          o.nodeId = this.nodeId;
          o.entity = this.entity;
          o.interval = this.interval;
          o.aggregate = this.aggregate;
          o.duration = this.duration;
        }

        this.removeAttr = function (name, attr) {
          if (this.datum[name]) {
            if (this.datum[name][attr]) {
              delete this.datum[name][attr]
            }
          }
          return this.attrs().length;
        }

        this.equals = function (r, entity, aggregate) {
          if (arguments.length == 3) {
            var o = {nodeId: r, entity: entity, aggregate: aggregate}
            r = o;
          }
          return (this.nodeId === r.nodeId && this.entity === r.entity && this.aggregate == r.aggregate)
        }
        this.names = function () {
          return Object.keys(this.datum)
        }
        this.attrs = function () {
          var attrs = {}
          Object.keys(this.datum).forEach( function (name) {
            Object.keys(this.datum[name]).forEach( function (attr) {
              attrs[attr] = 1;
            })
          }, this)
          return Object.keys(attrs);
        }
      };

      // Below here are the properties and methods available on QDRChartService
      var self = {
        charts: [],         // list of charts to gather data for
        chartRequests: [],  // the management request info (multiple charts can be driven off of a single request

        init: function () {
          self.loadCharts();
          QDRService.addDisconnectAction( function () {
            self.charts.forEach( function (chart) {
             self.unRegisterChart(chart, true)
            })
            QDRService.addConnectAction(self.init);
          })
        },

        findChartRequest: function (nodeId, entity, aggregate) {
          var ret = null;
          self.chartRequests.some( function (request) {
            if (request.equals(nodeId, entity, aggregate)) {
              ret = request;
              return true;
            }
          })
          return ret;
        },

        findCharts: function (opts) { //name, attr, nodeId, entity, hdash) {
          if (!opts.hdash)
              opts.hdash = false; // rather than undefined
          return self.charts.filter( function (chart) {
            return (chart.name() == opts.name &&
              chart.attr() == opts.attr &&
              chart.nodeId() == opts.nodeId &&
              chart.entity() == opts.entity &&
              chart.hdash == opts.hdash)
          });
        },

        delChartRequest: function (request) {
          for (var i=0; i<self.chartRequests.length; ++i) {
            var r = self.chartRequests[i];
            if (request.equals(r)) {
              QDR.log.debug("removed request: " + request.nodeId + " " + request.entity);
                self.chartRequests.splice(i, 1);
                self.stopCollecting(request);
                return;
            }
          }
        },

        delChart: function (chart, skipSave) {
          var foundBases = 0;
          for (var i=0; i<self.charts.length; ++i) {
            var c = self.charts[i];
            if (c.base === chart.base)
              ++foundBases;
              if (c.equals(chart)) {
                self.charts.splice(i, 1);
                if (chart.dashboard && !skipSave)
                  self.saveCharts();
              }
          }
          if (foundBases == 1) {
            var baseIndex = bases.indexOf(chart.base)
            bases.splice(baseIndex, 1);
          }
        },

        // create a chart that doesn't get saved/loaded and has it's own
        // request scheduler
        createRequestAndChart: function (opts) {
          var request = new ChartRequest(opts)
          var chart = new Chart(opts, request)
          return chart
        },

        // opts are nodeId, entity, name, attr, interval, instance, use_instance,
        // forceCreate, aggregate, hdash
        registerChart: function (opts) {
          var request = self.findChartRequest(opts.nodeId, opts.entity, opts.aggregate);
          if (request) {
            // add any new attr or name to the list
            request.addAttrName(opts.name, opts.attr)
          } else {
            // the nodeId/entity did not already exist, so add a new request and chart
            QDR.log.debug("added new request: " + opts.nodeId + " " + opts.entity);
            request = new ChartRequest(opts); //nodeId, entity, name, attr, interval, aggregate);
            self.chartRequests.push(request);
            self.startCollecting(request);
          }
          var charts = self.findCharts(opts); //name, attr, nodeId, entity, hdash);
          var chart;
          if (charts.length == 0 || opts.forceCreate) {
            if (!opts.use_instance && opts.instance)
              delete opts.instance;
            chart = new Chart(opts, request) //opts.name, opts.attr, opts.instance, request);
            self.charts.push(chart);
          } else {
            chart = charts[0];
          }
          return chart;
        },

        // remove the chart for name/attr
        // if all attrs are gone for this request, remove the request
        unRegisterChart: function (chart, skipSave) {
          // remove the chart

          // TODO: how do we remove charts that were added to the hawtio dashboard but then removed?
          // We don't get a notification that they were removed. Instead, we could just stop sending
          // the request in the background and only send the request when the chart's tick() event is triggered
          //if (chart.hdash) {
          //  chart.dashboard = false;
          //  self.saveCharts();
          //    return;
          //}

          for (var i=0; i<self.charts.length; ++i) {
            var c = self.charts[i];
            if (chart.equals(c)) {
              var request = chart.request();
              self.delChart(chart, skipSave);
              if (request) {
                // see if any other charts use this attr
                for (var i=0; i<self.charts.length; ++i) {
                  var c = self.charts[i];
                  if (c.attr() == chart.attr() && c.request().equals(chart.request()))
                    return;
                }
                // no other charts use this attr, so remove it
                if (request.removeAttr(chart.name(), chart.attr()) == 0) {
                  self.stopCollecting(request);
                  self.delChartRequest(request);
                }
              }
            }
          }
          if (!skipSave)
            self.saveCharts();
        },

        stopCollecting: function (request) {
          if (request.setTimeoutHandle) {
            clearTimeout(request.setTimeoutHandle);
            request.setTimeoutHandle = null;
          }
        },

        startCollecting: function (request) {
          // Using setTimeout instead of setInterval because the response may take longer than interval
          request.setTimeoutHandle = setTimeout(self.sendChartRequest, request.interval, request);
        },

        shouldRequest: function (request) {
          // see if any of the charts associated with this request have either dialog, dashboard, or hreq
          return self.charts.some( function (chart) {
            return (chart.dashboard || chart.hreq) || (!chart.dashboard && !chart.hdash);
          });
        },

        // send the request
        sendChartRequest: function (request, once) {
          if (!once && !self.shouldRequest(request)) {
            request.setTimeoutHandle = setTimeout(self.sendChartRequest, request.interval, request)
            return;
          }
          // ensure the response has the name field so we can associate the response values with the correct chart
          var attrs = request.attrs();
          attrs.push("name");

          // this is called when the response is received
          var saveResponse = function (nodeId, entity, response) {
            if (!response || !response.attributeNames)
              return;
            //QDR.log.debug("got chart results for " + nodeId + " " + entity);
            // records is an array that has data for all names
            var records = response.results;
            if (!records)
              return;

            var now = new Date();
            var cutOff = new Date(now.getTime() - request.duration * 60 * 1000);
            // index of the "name" attr in the response
            var nameIndex = response.attributeNames.indexOf("name");
            if (nameIndex < 0)
              return;

            var names = request.names();
            // for each record returned, find the name/attr for this request and save the data with this timestamp
            for (var i=0; i<records.length; ++i) {
              var name = records[i][nameIndex];
              // if we want to store the values for some attrs for this name
              if (names.indexOf(name) > -1) {
                attrs.forEach( function (attr) {
                  var data = request.data(name, attr) // get a reference to the data array
                  if (data) {
                    var attrIndex = response.attributeNames.indexOf(attr)
                    if (request.aggregate) {
                      data.push([now, response.aggregates[i][attrIndex].sum, response.aggregates[i][attrIndex].detail])
                    } else {
                      data.push([now, records[i][attrIndex]])
                    }
                    // expire the old data
                    while (data[0][0] < cutOff) {
                      data.shift();
                    }
                  }
                })
              }
            }
          }
          if (request.aggregate) {
            var nodeList = QDRService.nodeIdList()
            QDRService.getMultipleNodeInfo(nodeList, request.entity, attrs, saveResponse, request.nodeId);
          } else {
            QDRService.getNodeInfo(request.nodeId, request.entity, attrs, saveResponse);
          }
          // it is now safe to schedule another request
          if (once)
            return;
          request.setTimeoutHandle = setTimeout(self.sendChartRequest, request.interval, request)
        },

        numCharts: function () {
         return self.charts.filter( function (chart) { return chart.dashboard }).length;
         //return self.charts.length;
        },

        isAttrCharted: function (nodeId, entity, name, attr) {
          var charts = self.findCharts({
            name: name,
            attr: attr,
            nodeId: nodeId,
            entity: entity
          })
          // if any of the matching charts are on the dashboard page, return true
          return charts.some(function (chart) {
            return (chart.dashboard)
          });
        },

        addHDash: function (chart) {
          chart.hdash = true;
          self.saveCharts();
        },

        delHDash: function (chart) {
          chart.hdash = false;
          self.saveCharts();
        },

        addDashboard: function (chart) {
          chart.dashboard = true;
          self.saveCharts();
        },

        delDashboard: function (chart) {
          chart.dashboard = false;
          self.saveCharts();
        },

        // save the charts to local storage
        saveCharts: function () {
          var charts = [];
          var minCharts = [];

          self.charts.forEach(function (chart) {
            var minChart = {};
           // don't save chart unless it is on the dashboard
            if (chart.dashboard || chart.hdash) {
              chart.copyProps(minChart);
              minCharts.push(minChart);
            }
          })
          localStorage["QDRCharts"] = angular.toJson(minCharts);
        },

        loadCharts: function () {
          var charts = angular.fromJson(localStorage["QDRCharts"]);
          if (charts) {
            // get array of known ids
            var nodeList = QDRService.nodeList().map( function (node) {
              return node.id;
            })
            charts.forEach(function (chart) {
              // if this chart is not in the current list of nodes, skip
              if (nodeList.indexOf(chart.nodeId) >= 0) {
                if (!angular.isDefined(chart.instance)) {
                  chart.instance = ++instance;
                }
                if (chart.instance >= instance)
                  instance = chart.instance + 1;
                if (!chart.duration)
                    chart.duration = 10;
                if (chart.nodeList)
                    chart.aggregate = true;
                if (!chart.hdash)
                    chart.hdash = false;
                if (!chart.dashboard)
                    chart.dashboard = false;
                if (!chart.hdash && !chart.dashboard)
                    chart.dashboard = true;
                if (chart.hdash && chart.dashboard)
                  chart.dashboard = false;
                chart.forceCreate = true;
                chart.use_instance = true;
                var newChart = self.registerChart(chart); //chart.nodeId, chart.entity, chart.name, chart.attr, chart.interval, true, chart.aggregate);
                newChart.dashboard = chart.dashboard;
                newChart.hdash = chart.hdash;
                newChart.hreq = false;
                newChart.type = chart.type;
                newChart.rateWindow = chart.rateWindow;
                newChart.areaColor = chart.areaColor ? chart.areaColor : "#cbe7f3";
                newChart.lineColor = chart.lineColor ? chart.lineColor : "#058dc7";
                newChart.duration(chart.duration);
                newChart.visibleDuration = chart.visibleDuration ? chart.visibleDuration : 10;
                if (chart.userTitle)
                  newChart.title(chart.userTitle);
              }
            })
          }
        },

        AreaChart: function (chart) {
          if (!chart)
            return;

          // if this is an aggregate chart, show it stacked
          var stacked = chart.request().aggregate;
          this.chart = chart; // reference to underlying chart
          this.svgchart = null;
          this.url = $location.absUrl();

          // callback function. called by svgchart when binding data
          // the variable 'this' refers to the svg and not the AreaChart,
          // but since we are still in the scope of the AreaChart we have access to the passed in chart argument
          this.chartData = function () {

            var now = new Date();
            var visibleDate = new Date(now.getTime() - chart.visibleDuration * 60 * 1000);
            var data = chart.data();
            var nodeList = QDRService.nodeIdList();

            if (chart.type == "rate") {
              var rateData = [];
              var datalen = data.length;
              var k = 0;  // inner loop optimization
              for (var i=0; i<datalen; ++i) {
                var d = data[i];
                if (d[0] >= visibleDate) {
                  for (var j=k+1; j<datalen; ++j) {
                    var d1 = data[j];
                    if (d1[0] - d[0] >= chart.rateWindow) { // rateWindow is the timespan to calculate rates
                      var elapsed = Math.max((d1[0] - d[0]) / 1000, 1); // number of seconds that elapsed
                      var rd = [d1[0],(d1[1] - d[1])/elapsed]
                      k = j; // start here next time
                      // this is a stacked (aggregate) chart
                      if (stacked) {
                        var detail = [];
                        nodeList.forEach( function (node, nodeIndex) {
                          if (d1[2][nodeIndex] && d[2][nodeIndex])
                            detail.push({node: QDRService.nameFromId(node), val: (d1[2][nodeIndex].val- d[2][nodeIndex].val)/elapsed})
                          })
                        rd.push(detail)
                      }
                      rateData.push(rd);
                      break;
                    }
                  }
                }
              }
              // we need at least a point to chart
              if (rateData.length == 0) {
                rateData[0] = [chart.data()[0][0],0,[{node:'',val:0}]];
              }
              return rateData;
            }
            if (chart.visibleDuration != chart.duration()) {
              return data.filter(function (d) { return d[0]>=visibleDate});
            } else
              return data;
          }

          this.zoom = function (id, zoom) {
            if (this.svgchart) {
              this.svgchart.attr("zoom", zoom)
              d3.select('#' + id)
                .data([this.chartData()])
                .call(this.svgchart)
            }
          }

          // called by the controller on the page that displays the chart
          // called whenever the controller wants to redraw the chart
          // note: the data is collected independently of how often the chart is redrawn
          this.tick = function (id) {

            // can't draw charts that don't have data yet
            if (this.chart.data().length == 0) {
              return;
            }

            // if we haven't created the svg yet
            if (!this.svgchart) {

              // make sure the dom element exists on the page
              var div = angular.element('#' + id);
              if (!div)
                return;

              var width = div.width();
              var height = div.height();

              // make sure the dom element has a size. otherwise we wouldn't see anything anyway
              if (!width)
                return;

              var tooltipGenerator;
              // stacked charts have a different tooltip
              if (stacked) {
                tooltipGenerator = function (d, color, format) {
                  var html = "<table class='fo-table'><tbody><tr class='fo-title'>"+
                  "<td align='center' colspan='2' nowrap>Time: "+d[0].toTimeString().substring(0, 8)+"</td></tr>"
                  d[2].forEach( function (detail) {
                      html += "<tr class='detail'><td align='right' nowrap>"
                      + detail.node
                      + "<div class='fo-table-legend' style='background-color: "+color(detail.node)+"'></div>"
                      + "</td><td>"+format(detail.val)+"</td></tr>"
                  })
                  html += "</tbody></table>"
                  return html;
                }
              } else {
                tooltipGenerator = function (d, color, format) {
                  var html = "<table class='fo-table'><tbody><tr class='fo-title'>"+
                    "<td align='center'>Time</td><td align='center'>Value</td></tr><tr><td>" +
                        d[0].toTimeString().substring(0, 8) +
                    "</td><td>" +
                       format(d[1]) +
                    "</td></tr></tbody></table>"
                  return html;
                }
              }
              // create and initialize the chart
              this.svgchart = self.timeSeriesStackedChart(id, width, height,
                QDRService.humanify(this.chart.attr()),
                this.chart.name(),
                QDRService.nameFromId(this.chart.nodeId()) || '',
                this.chart.entity(),
                stacked,
                this.chart.visibleDuration)
              .tooltipGenerator(tooltipGenerator);
            }

            // in case the chart properties have changed, set the new props
            this.svgchart
              .attr("type", this.chart.type)
              .attr("areaColor", this.chart.areaColor)
              .attr("lineColor", this.chart.lineColor)
              .attr("url", this.url)
              .attr("title", this.chart.userTitle);

            // bind the new data and update the chart
            d3.select('#' + id)         // the div id on the page/dialog
              .data([this.chartData()])
              .call(this.svgchart);       // the charting function
          }
        },

      timeSeriesStackedChart: function (id, width, height, attrName, name, node, entity, stacked, visibleDuration) {
        var margin = {top: 20, right: 18, bottom: 10, left: 15}
        // attrs that can be changed after the chart is created by using
        // chart.attr(<attrname>, <attrvalue>);
        var attrs = {
              attrName: attrName, // like Deliveries to Container. Put at top of chart
              name: name,         // like router.address/qdrhello  Put at bottom of chart with node
              node: node,         // put at bottom of chart with name
              entity: entity,     // like .router.address  Not used atm
              title: "",          // user title overrides the node and name at the bottom of the chart
              url: "",            // needed to reference filters and clip because of angular's location service
              type: "value",      // value or rate
              areaColor: "",      // can be set for non-stacked charts
              lineColor: "",      // can be set for non-stacked charts
              zoom: false,         // should the y-axis range start at 0 or the min data value
              visibleDuration: visibleDuration
        }
        var width = width - margin.left - margin.right,
          height = height - margin.top - margin.bottom,
          yAxisTransitionDuration = 0

        var x = d3.time.scale()
          var y = d3.scale.linear()
                .rangeRound([height, 0]);
                // The x-accessor for the path generator; xScale * xValue.
        var X = function (d) { return x(d[0]) }
                // The x-accessor for the path generator; yScale * yValue.
                var Y = function Y(d) { return y(d[1]) }

                var xAxis = d3.svg.axis().scale(x).orient("bottom")
          .outerTickSize(6)
          .innerTickSize(-(height-margin.top-margin.bottom))
                    .tickPadding(2)
                    .ticks(d3.time.minutes, 2)
                var yAxis = d3.svg.axis().scale(y).orient("right")
                    .outerTickSize(8)
                    .innerTickSize(-(width-margin.left-margin.right))
                    .tickPadding(10)
                    .ticks(3)
                    .tickFormat(function(d) { return formatValue(d)})

        var tooltipGenerator = function (d, color, format) {return ""}; // should be overridden to set an appropriate tooltip
                var formatValue = d3.format(".2s");
                var formatPrecise = d3.format(",");
                var bisectDate = d3.bisector(function(d) { return d[0]; }).left;
        var line = d3.svg.line();

            var stack = d3.layout.stack()
                .offset("zero")
                .values(function (d) { return d.values; })
                .x(function (d) { return x(d.date); })
                .y(function (d) { return d.value; });

          var area = d3.svg.area()
        if (stacked) {
              area.interpolate("cardinal")
                .x(function (d) { return x(d.date); })
                .y0(function (d) { return y(d.y0); })
                .y1(function (d) { return y(d.y0 + d.y); });
        } else {
                    area.interpolate("basis").x(X).y1(Y)
                    line.x(X).y(Y)
        }
        var color = d3.scale.category20();

          var sv = d3.select("#"+id).append("svg")
                .attr("width",  width  + margin.left + margin.right)
                .attr("height", height + margin.top  + margin.bottom)
        var svg = sv
              .append("g")
                .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

        sv.append("linearGradient")
              .attr("id", id) //"temperature-gradient")
              .attr("gradientUnits", "userSpaceOnUse")
              .attr("x1", 0).attr("y1", height *.5 )
              .attr("x2", 0).attr("y2", height * 1.2)
            .selectAll("stop")
              .data([
                {offset: "0%", opacity: 1},
                {offset: "100%", opacity: 0}
              ])
            .enter().append("stop")
              .attr("offset", function(d) { return d.offset; })
              .attr("stop-opacity", function(d) { return d.opacity; })
              .attr("stop-color", function(d) { return "#cbe7f3" });
/*
            var clip = svg.append("defs").append("svg:clipPath")
              .attr("id", "clip")
              .append("svg:rect")
              .attr("id", "clip-rect")
              .attr("x", "0")
              .attr("y", "0")
              .attr("width", width)
              .attr("height", height);
*/
        // we want all our areas to appear before the axiis
        svg.append("g")
          .attr("class", "section-container")

            svg.append("g")
                  .attr("class", "x axis")

            svg.append("g")
                  .attr("class", "y axis")

                svg.append("text").attr("class", "title")
                        .attr("x", (width / 2) - (margin.left + margin.right) / 2)
                        .attr("y", 0 - (margin.top / 2))
                        .attr("text-anchor", "middle")
                        .text(attrs.attrName);

                svg.append("text").attr("class", "legend")
                        .attr("x", (width / 2) - (margin.left + margin.right) / 2)
                        .attr("y", height + (margin.bottom / 2) )
                        .attr("text-anchor", "middle")
                        .text(!stacked ? attrs.node + " " + attrs.name : attrs.name);

                var focus = sv.append("g")
                  .attr("class", "focus")
                  .style("display", "none");

                focus.append("circle")
                  .attr("r", 4.5);

                var focusg = focus.append("g");
                focusg.append("rect")
                    .attr("class", "mo-guide y")
                    .attr("width", 1)
                    .attr("height", height - (margin.top + margin.bottom));
                focusg.append("rect")
                    .attr("class", "mo-guide x")
                    .attr("width", width - (margin.left + margin.right))
                    .attr("height", 1);
        focus.append("foreignObject")
          .attr('class', 'svg-tooltip')
          .append("xhtml:span");
/*
        var transition = d3.select({}).transition()
            .duration(2000)
            .ease("linear");
*/
                function chart(selection) {
                    selection.each(function(data) {

          var seriesArr = []
          if (stacked) {
            var detailNames = data[0][2].map(function (detail){ return detail.node })
            var revNames = angular.copy(detailNames).reverse();
            color.domain(revNames);

                var series = {};
                detailNames.forEach(function (name) {
              series[name] = {name: name, values:[]};
              seriesArr.unshift(series[name]);    // insert at beginning
                });

                data.forEach(function (d) {
              detailNames.map(function (name, i) {
                series[name].values.push({date: d[0], value: d[2][i] ? d[2][i].val : 0});
              });
                });

                // this decorates seriesArr with x,y,and y0 properties
                stack(seriesArr);
          }

                    var extent = d3.extent(data, function(d) {return d[0];});
                    //var points = data.length;
          //var futureDate = new Date(data[points-1][0].getTime() - attrs.visibleDuration * 60 * 1000);
                    //extent = [futureDate, data[points-1][0]]
                    x.domain(extent)
                      .range([0, width - margin.left - margin.right]);

                    // Update the y-scale.
                    var min = attrs.zoom ? 0 : d3.min(data, function(d) {return d[1]}) *.99;
                    var max = d3.max(data, function(d) {return d[1]}) * 1.01;
                    var mean = d3.mean(data, function(d) {return d[1]});
                    //max = max * 1.01;
                    var diff = (max - min);
                    if (diff == 0) {
                        max = max + 1;
                        diff = 1;
                    }
                    var ratio = mean != 0 ? diff / mean : 1;
                    if (ratio < .05)
                        formatValue = d3.format(".3s")

          if (stacked) {
                      y.domain([min, max])
                        .range([height - margin.top - margin.bottom, 0]);
          } else {
                        y
                          .domain([min, max])
                          .range([height - margin.top - margin.bottom, 0]);
          }
                        if (attrs.type == "rate") {
                            area.interpolate("basis");  // rate charts look better smoothed
                            line.interpolate("basis");
                        }
                        else {
                            area.interpolate("linear"); // don't smooth value charts
                            line.interpolate("linear");
                        }

                    // adjust the xaxis based on the range of x values (domain)
                    var timeSpan = (extent[1] - extent[0]) / (1000 * 60);   // number of minutes
                    if (timeSpan < 1.5)
                        xAxis.ticks(d3.time.seconds, 10);
                    else if (timeSpan < 3)
                        xAxis.ticks(d3.time.seconds, 30);
                    else if (timeSpan < 8)
                        xAxis.ticks(d3.time.minutes, 1);
                    else
                        xAxis.ticks(d3.time.minutes, 2);

                    // adjust the number of yaxis ticks based on the range of y values
                    if (formatValue(min) === formatValue(max))
                        yAxis.ticks(2);

          var container = svg.select('.section-container');
          container.selectAll('.series').remove();
          if (stacked) {
                      y.domain([Math.min(min, 0), d3.max(seriesArr, function (c) {
                          return d3.max(c.values, function (d) { return d.y0 + d.y; });
                        })]);

            // creates a .series g path for each section in the detail
            // since we don't get more sections this selection is only run once
                      var series = container.selectAll(".series")
                        .data(seriesArr)

            series.enter().append("g")
                          .attr("class", "series")
                  .append("path")
              .attr("class", "streamPath")
              .style("fill", function (d) { return color(d.name); })
              .style("stroke", "grey");

            series.exit().remove()

            // each time the data is updated, update each section
            container.selectAll(".series .streamPath").data(seriesArr)
              .attr("d", function (d) { return area(d.values); })
          } else {
                      var series = container.selectAll(".series")
                        .data([data], function(d) { return d; })

            var g = series.enter().append("g")
              .attr("class", "series")

                      g.append("path")
                          .attr("class", "area")
              .style("fill", "url(" + attrs.url + "#" + id + ") " + attrs.areaColor) //temperature-gradient)")
                            .attr("d", area.y0(y.range()[0]))
                            .attr("transform", null);

                      g.append("path")
                          .attr("class", "line")
              .style("stroke", attrs.lineColor)
                            .attr("d", line)
/*
debugger;
            g.transition()
              .duration(2000)
                            .attr("transform", "translate(-4)");
*/
            series.exit().remove()

            sv.selectAll("stop")
                  .attr("stop-color", attrs.areaColor)

          }
                    // Update the x-axis.
                    svg.select(".x.axis")
                      .attr("transform", "translate(0," + (height - margin.top - margin.bottom + 1) + ")")
                      .call(xAxis);

                    svg.select(".y.axis")
            .transition().duration(yAxisTransitionDuration)  // animate the y axis
                      .attr("transform", "translate(" + (width - margin.right - margin.left) + ",0)")
                      .call(yAxis);
                    yAxisTransitionDuration = 1000  // only do a transition after the chart is 1st drawn

                    // TODO: fix this
                    // need to recreate this every update... not sure why
                    var overlay = sv.select(".overlay");
                    if (!overlay.empty())
                            overlay.remove();
                    sv.append("rect")
                      .attr("class", "overlay")
                      .attr("width", width)
                      .attr("height", height)
                      .on("mouseover", function () {focus.style("display", null)})
                      .on("mouseout", function () {focus.style("display", "none")})
                      .on("mousemove", mousemove)

                      function mousemove() {
                          var x0 = x.invert(d3.mouse(this)[0] - margin.left);
                          var i = bisectDate(data, x0, 1);
                          if (i < data.length && i > 0) {
                              var d0 = data[i - 1];
                              var d1 = data[i];
                // set d to the data that is closest to the mouse position
                              var d = x0 - d0[0] > d1[0] - x0 ? d1 : d0;
                              focus.attr("transform", "translate(" + (x(d[0]) + margin.left) + "," + (y(d[1]) + margin.top) + ")");

                var tipFormat = formatPrecise;
                if (attrs.type === "rate")
                  tipFormat = d3.format(".2n")
                // set the tooltip html and position it
                focus.select('.svg-tooltip span')
                  .html(tooltipGenerator(d, color, tipFormat))

                var foBounds = focus.select('table')[0][0].getBoundingClientRect();
                              var mx = x(d[0]); // mouse x
                              var my = y(d[1]); // mouse y

                              // perfer to put the tooltip in the nw corner relative to the focus circle
                              var foy = -foBounds.height;
                var fox = -foBounds.width;
                // off the left side
                if (mx - foBounds.width - margin.left < 0)
                  fox = 0;
                // above the top
                if (my - foBounds.height - margin.top < 0)
                  foy = 0;
                // won't fit above or below, just put it at bottom
                if (my + foBounds.height > height)
                  foy = -(foBounds.height - (height - my));

                focus.select('.svg-tooltip')
                  .attr('x', fox).attr('y', foy);

                // position the guide lines
                              focus.select(".mo-guide.y")
                                  .attr("y", -my);
                              focus.select(".mo-guide.x")
                                  .attr("x", -mx);

                          } else {
                              focus.attr("transform", "translate(-10,-10)");
                          }
                      }

                    })


                }
        chart.attr = function (attrName, value) {
          if (arguments.length < 2)
            return arguments.length == 1 ? attrs[attrName] : chart;
          if (angular.isDefined(attrs[attrName]))
            attrs[attrName] = value;
          return chart;
        }
        chart.tooltipGenerator = function (_) {
          tooltipGenerator = _;
          return chart;
        }

        return chart;
            }
        }
        return self;
  };

  return QDR;
}(QDR || {}));
