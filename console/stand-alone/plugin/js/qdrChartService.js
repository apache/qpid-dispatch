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
/* global angular d3 c3 */

import { QDRLogger } from './qdrGlobals.js';

let instance = 0,
  bases = [];

class ChartBase {
  constructor(name, attr, request) {
    // the base chart attributes
    this.name = name; // the record's "name" field
    this.attr = attr; // the record's attr field to chart
    this.request = request; // the associated request that fetches the data
    // copy the savable properties to an object
    this.copyProps = function (o) {
      o.name = this.name;
      o.attr = this.attr;
      this.request.copyProps(o);
    };
    this.equals = function (name, attr, request) {
      return (this.name == name && this.attr == attr && this.request.equals(request));
    };
  }
}

// Object that represents a visible chart
// There can be multiple of these per ChartBase (eg. one rate  and one value chart)
class Chart {
  constructor(opts, request, QDRService, QDRChartService) {
    var findBase = function (name, attr, request) {
      for (let i = 0; i < bases.length; ++i) {
        let base = bases[i];
        if (base.equals(name, attr, request))
          return base;
      }
      return null;
    };
    let base = findBase(opts.name, opts.attr, request);
    if (!base) {
      base = new ChartBase(opts.name, opts.attr, request);
      bases.push(base);
    }
    this.base = base;
    this.QDRService = QDRService;
    this.QDRChartService = QDRChartService;
    this.instance = angular.isDefined(opts.instance) ? opts.instance : ++instance;
    this.dashboard = false; // is this chart on the dashboard page
    this.hdash = false; // is this chart on the hawtio dashboard page
    this.hreq = false; // has this hdash chart been requested
    this.type = opts.type ? opts.type : 'value'; // value or rate
    this.rateWindow = opts.rateWindow ? opts.rateWindow : 1000; // calculate the rate of change over this time interval. higher == smother graph
    this.areaColor = '#32b9f3'; // the chart's area color when not an empty string
    this.lineColor = '#058dc7'; // the chart's line color when not an empty string
    this.visibleDuration = opts.visibleDuration ? opts.visibleDuration : opts.type === 'rate' ? 0.25 : 1; // number of minutes of data to show (<= base.duration)
    this.userTitle = null; // user title overrides title()
    this.hideLabel = opts.hideLabel;
    this.hideLegend = opts.hideLegend;
    // generate a unique id for this chart
    this.id = function () {
      let name = this.name();
      let nameparts = name.split('/');
      if (nameparts.length == 2)
        name = nameparts[1];
      let key = this.QDRService.utilities.nameFromId(this.request().nodeId) + this.request().entity + name + this.attr() + '_' + this.instance + '_' + (this.request().aggregate ? '1' : '0');
      // remove all characters except letters,numbers, and _
      return key.replace(/[^\w]/gi, '');
    };
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
    };
    this.name = function (_) {
      if (!arguments.length)
        return this.base.name;
      this.base.name = _;
      return this;
    };
    this.attr = function (_) {
      if (!arguments.length)
        return this.base.attr;
      this.base.attr = _;
      return this;
    };
    this.nodeId = function (_) {
      if (!arguments.length)
        return this.base.request.nodeId;
      this.base.request.nodeId = _;
      return this;
    };
    this.entity = function (_) {
      if (!arguments.length)
        return this.base.request.entity;
      this.base.request.entity = _;
      return this;
    };
    this.aggregate = function (_) {
      if (!arguments.length)
        return this.base.request.aggregate;
      this.base.request.aggregate = _;
      return this;
    };
    this.request = function (_) {
      if (!arguments.length)
        return this.base.request;
      this.base.request = _;
      return this;
    };
    this.data = function () {
      return this.base.request.data(this.base.name, this.base.attr); // refernce to chart's data array
    };
    this.interval = function (_) {
      if (!arguments.length)
        return this.base.request.interval;
      this.base.request.interval = _;
      return this;
    };
    this.duration = function (_) {
      if (!arguments.length)
        return this.base.request.duration;
      this.base.request.duration = _;
      return this;
    };
    this.router = function () {
      return this.QDRService.utilities.nameFromId(this.nodeId());
    };
    this.title = function (_) {
      let name = this.request().aggregate ? 'Aggregate' : this.QDRService.utilities.nameFromId(this.nodeId());
      let computed = name +
              ' ' + this.QDRService.utilities.humanify(this.attr()) +
              ' - ' + this.name();
      if (!arguments.length)
        return this.userTitle || computed;
      // don't store computed title in userTitle
      if (_ === computed)
        _ = null;
      this.userTitle = _;
      return this;
    };
    this.title_short = function () {
      if (!arguments.length)
        return this.userTitle || this.name();
      return this;
    };
    this.copy = function () {
      let chart = this.QDRChartService.registerChart({
        nodeId: this.nodeId(),
        entity: this.entity(),
        name: this.name(),
        attr: this.attr(),
        interval: this.interval(),
        forceCreate: true,
        aggregate: this.aggregate(),
        hdash: this.hdash
      });
      chart.type = this.type;
      chart.areaColor = this.areaColor;
      chart.lineColor = this.lineColor;
      chart.rateWindow = this.rateWindow;
      chart.visibleDuration = this.visibleDuration;
      chart.userTitle = this.userTitle;
      return chart;
    };
    // compare to a chart
    this.equals = function (c) {
      return (c.instance == this.instance &&
              c.base.equals(this.base.name, this.base.attr, this.base.request) &&
              c.type == this.type &&
              c.rateWindow == this.rateWindow &&
              c.areaColor == this.areaColor &&
              c.lineColor == this.lineColor);
    };
  }
}


// Object that represents the management request to fetch and store data for multiple charts
class ChartRequest {
  constructor(opts) {
    this.duration = opts.duration || 10; // number of minutes to keep the data
    this.nodeId = opts.nodeId; // eg amqp:/_topo/0/QDR.A/$management
    this.entity = opts.entity; // eg .router.address
    // sorted since the responses will always be sorted
    this.aggregate = opts.aggregate; // list of nodeIds for aggregate charts
    this.datum = {}; // object containing array of arrays for each attr
    // like {attr1: [[date,value],[date,value]...], attr2: [[date,value]...]}
    this.interval = opts.interval || 1000; // number of milliseconds between updates to data
    this.setTimeoutHandle = null; // used to cancel the next request
    // allow override of normal request's management call to get data
    this.override = opts.override; // call this instead of internal function to retreive data
    this.overrideAttrs = opts.overrideAttrs;
    this.data = function (name, attr) {
      if (this.datum[name] && this.datum[name][attr])
        return this.datum[name][attr];
      return null;
    };
    this.addAttrName = function (name, attr) {
      if (Object.keys(this.datum).indexOf(name) == -1) {
        this.datum[name] = {};
      }
      if (Object.keys(this.datum[name]).indexOf(attr) == -1) {
        this.datum[name][attr] = [];
      }
    };
    this.addAttrName(opts.name, opts.attr);
    this.copyProps = function (o) {
      o.nodeId = this.nodeId;
      o.entity = this.entity;
      o.interval = this.interval;
      o.aggregate = this.aggregate;
      o.duration = this.duration;
    };
    this.removeAttr = function (name, attr) {
      if (this.datum[name]) {
        if (this.datum[name][attr]) {
          delete this.datum[name][attr];
        }
      }
      return this.attrs().length;
    };
    this.equals = function (r, entity, aggregate) {
      if (arguments.length == 3) {
        let o = {
          nodeId: r,
          entity: entity,
          aggregate: aggregate
        };
        r = o;
      }
      return (this.nodeId === r.nodeId && this.entity === r.entity && this.aggregate == r.aggregate);
    };
    this.names = function () {
      return Object.keys(this.datum);
    };
    this.attrs = function () {
      let attrs = {};
      Object.keys(this.datum).forEach(function (name) {
        Object.keys(this.datum[name]).forEach(function (attr) {
          attrs[attr] = 1;
        });
      }, this);
      return Object.keys(attrs);
    };
  }
}

class AreaChart {
  constructor(chart, chartId, defer, width, QDRService) {
    if (!chart)
      return;
    this.QDRService = QDRService;
    // reference to underlying chart
    this.chart = chart;
    // if this is an aggregate chart, show it stacked
    this.stacked = chart.request().aggregate;
    // the id of the html element that is bound to the chart. The svg will be a child of this
    this.htmlId = chartId;
    this.colorMap = {};
    if (!defer)
      this.generate(width);
  }

  // create the svg and bind it to the given div.id
  generate (width) {
    let chart = this.chart;  // for access during chart callbacks
    let self = this;

    // an array of 10 colors
    let colors = d3.scale.category10().range();
    // list of router names. used to get the color index
    let nameList = this.QDRService.management.topology.nodeNameList();
    for (let i=0; i<nameList.length; i++) {
      this.colorMap[nameList[i]] = colors[i % 10];
    }
    let nodeName = this.QDRService.utilities.nameFromId(this.chart.base.request.nodeId);
    this.colorMap[nodeName] = this.chart.areaColor;

    let c3ChartDefaults = $().c3ChartDefaults();
    let singleAreaChartConfig = c3ChartDefaults.getDefaultSingleAreaConfig();
    singleAreaChartConfig.bindto = '#' + this.htmlId;
    singleAreaChartConfig.size = {
      width: width || 400,
      height: 200
    };
    singleAreaChartConfig.data = {
      x: 'x',           // x-axis is named x
      columns: [[]],
      type: 'area-spline'
    };
    singleAreaChartConfig.axis = {
      x: {
        type: 'timeseries',
        tick: {
          format: (function (d) {
            let data = this.singleAreaChart.data.shown();
            let first = data[0]['values'][0].x;

            if (d - first == 0) {
              return d3.timeFormat('%I:%M:%S')(d);
            }
            return d3.timeFormat('%M:%S')(d);
          }).bind(this),
          culling: {max: 4}
        }
      },
      y: {
        tick: {
          format: function (d) { return d<1 ? d3.format('.2f')(d) : d3.format('.2s')(d); },
          count: 5
        }
      }
    };

    if (!chart.hideLabel) {
      singleAreaChartConfig.axis.x.label = {
        text: chart.name(),
        position: 'outer-right'
      };

    }
    singleAreaChartConfig.transition = {
      duration: 0
    };

    singleAreaChartConfig.area = {
      zerobased: false
    };

    singleAreaChartConfig.tooltip = {
      contents: function (d) {
        let d3f = ',';
        if (chart.type === 'rate')
          d3f = ',.2f';
        let zPre = function (i) {
          if (i < 10) {
            i = '0' + i;
          }
          return i;
        };
        let h = zPre(d[0].x.getHours());
        let m = zPre(d[0].x.getMinutes());
        let s = zPre(d[0].x.getSeconds());
        let table = '<table class=\'dispatch-c3-tooltip\'>  <tr><th colspan=\'2\' class=\'text-center\'><strong>'+h+':'+m+':'+s+'</strong></th></tr> <tbody>';
        for (let i=0; i<d.length; i++) {
          let c = self.colorMap[d[i].id];
          let span = `<span class='chart-tip-legend' style='background-color: ${c};'> </span>` + d[i].id;
          table += ('<tr><td>'+span+'<td>'+d3.format(d3f)(d[i].value)+'</td></tr>');
        }
        table += '</tbody></table>';
        return table;
      }
    };

    singleAreaChartConfig.title = {
      text: this.QDRService.utilities.humanify(this.chart.attr())
    };

    singleAreaChartConfig.data.color = (color, d) => {
      let c = this.colorMap[d.id];
      return c ? c : color;
    };
    singleAreaChartConfig.color.pattern[0] = this.chart.areaColor;

    //singleAreaChartConfig.data.colors = {};
    //nameList.forEach( (name, i) => singleAreaChartConfig.data.colors[name] = this.colorMap[name] );
    singleAreaChartConfig.data.colors = this.colormap;

    if (!chart.hideLegend) {
      singleAreaChartConfig.legend = {
        show: true,
      };
    }

    if (this.stacked) {
      // create a stacked area chart
      singleAreaChartConfig.data.groups = [this.QDRService.management.topology.nodeNameList()];
      singleAreaChartConfig.data.order = function (t1, t2) { return t1.id < t2.id; };
    }

    this.singleAreaChart = c3.generate(singleAreaChartConfig);
  }

  chartData() {
    let data = this.chart.data();
    let nodeList = this.QDRService.management.topology.nodeNameList();

    // oldest data point that should be visible
    let now = new Date();
    let visibleDate = new Date(now.getTime() - this.chart.visibleDuration * 60 * 1000);

    let accessorSingle = function (d, d1, elapsed) {
      return this.chart.type === 'rate' ? (d1[1] - d[1]) / elapsed : d[1];
    };
    let accessorStacked = function (d, d1, elapsed, i) {
      return this.chart.type === 'rate' ? (d1[2][i].val - d[2][i].val) / elapsed : d[2][i].val;
    };
    let accessor = this.stacked ? accessorStacked : accessorSingle;

    let dx = ['x'];
    let dlines = [];
    if (this.stacked) {
      // for stacked, there is a line per router
      nodeList.forEach( function (node) {
        dlines.push([node]);
      });
    } else {
      // for non-stacked, there is only one line
      dlines.push([this.aggregate ? 'Total' : this.chart.router()]);
    }
    for (let i=0; i<data.length; i++) {
      let d = data[i], elapsed = 1, d1;
      if (d[0] >= visibleDate) {
        if (this.chart.type === 'rate' && i < data.length-1) {
          d1 = data[i+1];
          elapsed = Math.max((d1[0] - d[0]) / 1000, 0.001); // number of seconds that elapsed
        }
        // don't push the last data point for a rate chart
        if (this.chart.type !== 'rate' || i < data.length-1) {
          dx.push(d[0]);
          if (this.stacked) {
            for (let nodeIndex=0; nodeIndex<nodeList.length; nodeIndex++) {
              dlines[nodeIndex].push(accessor.call(this, d, d1, elapsed, nodeIndex));
            }
          } else {
            dlines[0].push(accessor.call(this, d, d1, elapsed));
          }
        }
      }
    }
    let columns = [dx];
    dlines.forEach( function (line) {
      columns.push(line);
    });
    return columns;
  }

  // get the data for the chart and update it
  tick () {
    // can't draw charts that don't have data yet
    if (!this.chart.data() || this.chart.data().length == 0 || !this.singleAreaChart) {
      return;
    }
    let nodeName = this.QDRService.utilities.nameFromId(this.chart.base.request.nodeId);
    this.colorMap[nodeName] = this.chart.areaColor;
    this.singleAreaChart.data.colors(this.colorMap);

    // update the chart title
    // since there is no c3 api to get or set the chart title, we change the title directly using d3
    let rate = '';
    if (this.chart.type === 'rate')
      rate = ' per second';
    d3.select('#'+this.htmlId+' svg text.c3-title').text(this.QDRService.utilities.humanify(this.chart.attr()) + rate);

    let d = this.chartData();
    // load the new data
    // using the c3.flow api causes the x-axis labels to jump around
    this.singleAreaChart.load({
      columns: d
    });
  }

}

// aggregate chart is based on pfAreaChart
class AggChart extends AreaChart {
  constructor(chart, chartId, defer, QDRService) {
    // inherit pfChart's properties, but force a defer
    super(chart, chartId, true, undefined, QDRService);
    // the request is for aggregate data, but the chart is for the sum and not the detail
    // Explanation: When the chart.request is aggregate, each data point is composed of 3 parts:
    //  1. the datetime stamp
    //  2. the sum of the value for all routers
    //  3. an object with each router's name and value for this data point
    // Normally, an aggregate chart shows lines for each of the routers and ignores the sum
    // For this chart, we want to chart the sum (the 2nd value), so we set stacked to false
    this.stacked = false;
    // let chart legends and tooltips show 'Total' instead of a router name
    this.aggregate = true;
    if (!defer)
      this.generate();
  }
}


export class QDRChartService {
  constructor(QDRService, $log) {
    this.charts = [];
    this.chartRequests = [];
    this.QDRService = QDRService;
    this.QDRLog = new QDRLogger($log, 'QDRChartService');
  }
  
  // Example service function
  init () {
    let self = this;
    this.loadCharts();
    this.QDRService.management.connection.addDisconnectAction(function() {
      self.charts.forEach(function(chart) {
        self.unRegisterChart(chart, true);
      });
      self.QDRService.management.connection.addConnectAction(self.init);
    });
  }
  findChartRequest (nodeId, entity, aggregate) {
    let ret = null;
    this.chartRequests.some(function(request) {
      if (request.equals(nodeId, entity, aggregate)) {
        ret = request;
        return true;
      }
    });
    return ret;
  }
  findCharts (opts) { //name, attr, nodeId, entity, hdash) {
    if (!opts.hdash)
      opts.hdash = false; // rather than undefined
    return this.charts.filter(function(chart) {
      return (chart.name() == opts.name &&
        chart.attr() == opts.attr &&
        chart.nodeId() == opts.nodeId &&
        chart.entity() == opts.entity &&
        chart.hdash == opts.hdash);
    });
  }

  delChartRequest (request) {
    for (let i = 0; i < this.chartRequests.length; ++i) {
      let r = this.chartRequests[i];
      if (request.equals(r)) {
        this.QDRLog.debug('removed request: ' + request.nodeId + ' ' + request.entity);
        this.chartRequests.splice(i, 1);
        this.stopCollecting(request);
        return;
      }
    }
  }

  delChart (chart, skipSave) {
    let foundBases = 0;
    for (let i = 0; i < this.charts.length; ++i) {
      let c = this.charts[i];
      if (c.base === chart.base)
        ++foundBases;
      if (c.equals(chart)) {
        this.charts.splice(i, 1);
        if (chart.dashboard && !skipSave)
          this.saveCharts();
      }
    }
    if (foundBases == 1) {
      let baseIndex = bases.indexOf(chart.base);
      bases.splice(baseIndex, 1);
    }
  }

  createChart (opts, request) {
    return new Chart(opts, request, this.QDRService, this);
  }
  createChartRequest (opts) {
    let request = new ChartRequest(opts); //nodeId, entity, name, attr, interval, aggregate);
    request.creationTimestamp = opts.now;
    this.chartRequests.push(request);
    this.startCollecting(request);
    this.sendChartRequest(request);
    return request;
  }
  destroyChartRequest (request) {
    this.stopCollecting(request);
    this.delChartRequest(request);
  }

  registerChart (opts) { //nodeId, entity, name, attr, interval, instance, forceCreate, aggregate, hdash) {
    let request = this.findChartRequest(opts.nodeId, opts.entity, opts.aggregate);
    if (request) {
      // add any new attr or name to the list
      request.addAttrName(opts.name, opts.attr);
    } else {
      // the nodeId/entity did not already exist, so add a new request and chart
      this.QDRLog.debug('added new request: ' + opts.nodeId + ' ' + opts.entity);
      request = this.createChartRequest(opts);
    }
    let charts = this.findCharts(opts); //name, attr, nodeId, entity, hdash);
    let chart;
    if (charts.length == 0 || opts.forceCreate) {
      if (!opts.use_instance && opts.instance)
        delete opts.instance;
      chart = new Chart(opts, request, this.QDRService, this); //opts.name, opts.attr, opts.instance, request);
      this.charts.push(chart);
    } else {
      chart = charts[0];
    }
    return chart;
  }

  // remove the chart for name/attr
  // if all attrs are gone for this request, remove the request
  unRegisterChart (chart, skipSave) {
    // remove the chart
    for (let i = 0; i < this.charts.length; ++i) {
      let c = this.charts[i];
      if (chart.equals(c)) {
        let request = chart.request();
        this.delChart(chart, skipSave);
        if (request) {
          // see if any other charts use this attr
          for (let j = 0; j < this.charts.length; ++j) {
            let ch = this.charts[j];
            if (ch.attr() == chart.attr() && ch.request().equals(chart.request()))
              return;
          }
          // no other charts use this attr, so remove it
          if (request.removeAttr(chart.name(), chart.attr()) == 0) {
            this.destroyChartRequest(request);
          }
        }
      }
    }
    if (!skipSave)
      this.saveCharts();
  }

  stopCollecting (request) {
    if (request.setTimeoutHandle) {
      clearInterval(request.setTimeoutHandle);
      request.setTimeoutHandle = null;
    }
  }

  startCollecting (request) {
    request.setTimeoutHandle = setInterval(this.sendChartRequest.bind(this), request.interval, request);
  }
  shouldRequest () {
    // see if any of the charts associated with this request have either dialog, dashboard, or hreq
    return this.charts.some(function(chart) {
      return (chart.dashboard || chart.hreq) || (!chart.dashboard && !chart.hdash);
    });
  }
  // send the request
  sendChartRequest (request) {
    if (request.busy)
      return;
    if (this.charts.length > 0 && !this.shouldRequest(request)) {
      return;
    }
    // ensure the response has the name field so we can associate the response values with the correct chart
    let attrs = request.attrs();
    if (attrs.indexOf('name') == -1)
      attrs.push('name');

    // this is called when the response is received
    var saveResponse = function(nodeId, entity, response) {
      request.busy = false;
      if (!response || !response.attributeNames)
        return;
      // records is an array that has data for all names
      let records = response.results;
      if (!records)
        return;

      let now = new Date();
      let cutOff = new Date(now.getTime() - request.duration * 60 * 1000);
      // index of the "name" attr in the response
      let nameIndex = response.attributeNames.indexOf('name');
      if (nameIndex < 0)
        return;

      let names = request.names();
      // for each record returned, find the name/attr for this request and save the data with this timestamp
      for (let i = 0; i < records.length; ++i) {
        let name = records[i][nameIndex];
        // if we want to store the values for some attrs for this name
        if (names.indexOf(name) > -1) {
          attrs.forEach(function(attr) {
            let attrIndex = response.attributeNames.indexOf(attr);
            if (records[i][attrIndex] !== undefined) {
              let data = request.data(name, attr); // get a reference to the data array
              if (data) {

                if (request.aggregate) {
                  data.push([now, response.aggregates[i][attrIndex].sum, response.aggregates[i][attrIndex].detail]);
                } else {
                  data.push([now, records[i][attrIndex]]);
                }
                // expire the old data
                while (data[0][0] < cutOff) {
                  data.shift();
                }
              }
            }
          });
        }
      }
    };
    request.busy = true;
    // check for override of request
    if (request.override) {
      request.override(request, saveResponse);
    } else {
      // send the appropriate request
      if (request.aggregate) {
        let nodeList = this.QDRService.management.topology.nodeIdList();
        this.QDRService.management.topology.getMultipleNodeInfo(nodeList, request.entity, attrs, saveResponse, request.nodeId);
      } else {
        this.QDRService.management.topology.fetchEntity(request.nodeId, request.entity, attrs, saveResponse);
      }
    }
  }

  numCharts () {
    return this.charts.filter(function(chart) {
      return chart.dashboard;
    }).length;
    //return this.charts.length;
  }

  isAttrCharted (nodeId, entity, name, attr, aggregate) {
    let charts = this.findCharts({
      name: name,
      attr: attr,
      nodeId: nodeId,
      entity: entity
    });
    // if any of the matching charts are on the dashboard page, return true
    return charts.some(function(chart) {
      return (chart.dashboard && (aggregate ? chart.aggregate() : !chart.aggregate()));
    });
  }

  addHDash (chart) {
    chart.hdash = true;
    this.saveCharts();
  }
  delHDash (chart) {
    chart.hdash = false;
    this.saveCharts();
  }
  addDashboard (chart) {
    chart.dashboard = true;
    this.saveCharts();
  }
  delDashboard (chart) {
    chart.dashboard = false;
    this.saveCharts();
  }
  // save the charts to local storage
  saveCharts () {
    let minCharts = [];

    this.charts.forEach(function(chart) {
      let minChart = {};
      // don't save chart unless it is on the dashboard
      if (chart.dashboard || chart.hdash) {
        chart.copyProps(minChart);
        minCharts.push(minChart);
      }
    });
    localStorage['QDRCharts'] = angular.toJson(minCharts);
  }
  loadCharts () {
    let charts = angular.fromJson(localStorage['QDRCharts']);
    if (charts) {
      let self = this;
      // get array of known ids
      let nodeList = this.QDRService.management.topology.nodeIdList();
      charts.forEach(function(chart) {
        // if this chart is not in the current list of nodes, skip
        if (nodeList.indexOf(chart.nodeId) >= 0) {
          if (!angular.isDefined(chart.instance)) {
            chart.instance = ++instance;
          }
          if (chart.instance >= instance)
            instance = chart.instance + 1;
          if (!chart.duration)
            chart.duration = 1;
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
          let newChart = self.registerChart(chart); //chart.nodeId, chart.entity, chart.name, chart.attr, chart.interval, true, chart.aggregate);
          newChart.dashboard = chart.dashboard;
          newChart.hdash = chart.hdash;
          newChart.hreq = false;
          newChart.type = chart.type;
          newChart.rateWindow = chart.rateWindow;
          newChart.areaColor = chart.areaColor ? chart.areaColor : '#32b9f3';
          newChart.lineColor = chart.lineColor ? chart.lineColor : '#058dc7';
          newChart.duration(chart.duration);
          newChart.visibleDuration = chart.visibleDuration ? chart.visibleDuration : newChart.type === 'rate' ? 0.25 : 1;
          if (chart.userTitle)
            newChart.title(chart.userTitle);
        }
      });
    }
  }
  // constructor for a c3 area chart
  pfAreaChart (chart, chartId, defer, width) {
    return new AreaChart(chart, chartId, defer, width, this.QDRService);
  }

  // aggregate chart is based on pfAreaChart
  pfAggChart (chart, chartId, defer) {
    return new AggChart(chart, chartId, defer, this.QDRService);
  }
}

QDRChartService.$inject = ['QDRService', '$log'];