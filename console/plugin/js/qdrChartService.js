/**
 * @module QDR
 */
var QDR = (function(QDR) {

    // The QDR chart service handles periodic gathering data for charts and displaying the charts
    QDR.module.factory("QDRChartService", function($rootScope, QDRService, localStorage, $http, $resource) {

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
            this.request = request;     // the associated request that gets the data
            this.data = [];             // array of objects {date: value:}
            this.duration = 10;         // number of minutes to keep the data

            // copy the savable properties to an object
            this.copyProps = function (o) {
                o.name = this.name;
                o.attr = this.attr;
                o.duration = this.duration;
                this.request.copyProps(o);
            }

            this.equals = function (name, attr, request) {
                return (this.name == name && this.attr == attr && this.request.equals(request));
            }
        };

        // Object that represents a visible chart
        // There can be multiple of these per ChartBase (eg. one rate  and one value chart)
        function Chart(name, attr, request) {

            var base = findBase(name, attr, request);
            if (!base) {
                base = new ChartBase(name, attr, request);
                bases.push(base);
            }
            this.base = base;
            this.instance = instance++;
            this.dashboard = false;     // is this chart on the dashboard page
            this.type = "value";        // value or rate
            this.rateWindow = 1000;     // calculate the rate of change over this time interval. higher == smother graph
            this.areaColor = "#EEFFEE"; // the chart's area color when not an empty string
            this.lineColor = "#4682b4"; // the chart's line color when not an empty string
            this.visibleDuration = 10;  // number of minutes of data to show (<= base.duration)
            this.userTitle = null;      // user title overrides title()

            // generate a unique id for this chart
            this.id = function () {
                var key = this.request.nodeId + this.request.entity + this.name + this.attr + "_" + this.instance;
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
            this.request = function (_) {
                if (!arguments.length) return this.base.request;
                this.base.request = _;
                return this;
            }
            this.data = function () {
                if (!arguments.length) return this.base.data;   // read only
            }
            this.interval = function (_) {
                if (!arguments.length) return this.base.request.interval;
                this.base.request.interval = _;
                return this;
            }
            this.duration = function (_) {
                if (!arguments.length) return this.base.duration;
                this.base.duration = _;
                return this;
            }
            this.title = function (_) {
                var computed = QDRService.nameFromId(this.nodeId()) +
                                   "  :  " + this.name() +
                                   "  :  " + QDRService.humanify(this.attr());
                if (!arguments.length) return this.userTitle || computed;

                // don't store computed title in userTitle
                if (_ === computed)
                    _ = null;
                this.userTitle = _;
                return this;
            }
            this.copy = function () {
                var chart = self.registerChart(this.nodeId(), this.entity(),
                            this.name(), this.attr(), this.interval(), true);
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

        // Object that represents the management request to get data for multiple charts
        function ChartRequest(nodeId, entity, attr, interval) {
            this.nodeId = nodeId;       // eg. amqp:/_topo/0/QDR.A/$management
            this.entity = entity;       // eg .router.address
            this.attrs = [attr];        // deliveriesFromContainer

            this.interval = interval;   // number of milliseconds to update data
            this.setTimeoutHandle = null;   // used to cancel the next request
            // copy the savable properties to an object
            this.copyProps = function (o) {
                o.nodeId = this.nodeId;
                o.entity = this.entity;
                o.interval = this.interval;
            }

            this.equals = function (r) {
                return (this.nodeId == r.nodeId && this.entity == r.entity)
            }
        };

        var self = {
            charts: [],         // list of charts to gather data for
            chartRequests: [],  // the management request info (multiple charts can be driven off of a single request

            init: function () {
                self.loadCharts();
            },

            findChartRequest: function (nodeId, entity) {
                for (var i=0; i<self.chartRequests.length; ++i) {
                    var request = self.chartRequests[i];
                    if (request.nodeId == nodeId && request.entity == entity) {
                        return request;
                    }
                }
                return null;
            },

            findCharts: function (name, attr, nodeId, entity) {
                return self.charts.filter( function (chart) {
                    return (chart.name() == name &&
                            chart.attr() == attr &&
                            chart.nodeId() == nodeId &&
                            chart.entity() == entity)
                });
            },

            delChartRequest: function (request) {
                for (var i=0; i<self.chartRequests.length; ++i) {
                    var r = self.chartRequests[i];
                    if (request.equals(r)) {
                        self.chartRequests.splice(i, 1);
                        self.stopCollecting(request);
                        delete request;
                        return;
                    }
                }
            },

            delChart: function (chart) {
                for (var i=0; i<self.charts.length; ++i) {
                    var c = self.charts[i];
                    if (c.equals(chart)) {
                        self.charts.splice(i, 1);
                        if (chart.dashboard)
                            self.saveCharts();
                        delete chart;
                        return;
                    }
                }
            },

            registerChart: function (nodeId, entity, name, attr, interval, forceCreate) {
                var request = self.findChartRequest(nodeId, entity);
                if (request) {
                    // add any new attr to the list
                    if (request.attrs.indexOf(attr) == -1)
                        request.attrs.push(attr);
                } else {
                    // the nodeId/entity did not already exist, so add a new request and chart
                    QDR.log.debug("added new request: " + nodeId + " " + entity);
                    request = new ChartRequest(nodeId, entity, attr, interval);
                    self.chartRequests.push(request);
                    self.startCollecting(request);
                }
                var charts = self.findCharts(name, attr, nodeId, entity);
                var chart;
                if (charts.length == 0 || forceCreate) {
                    chart = new Chart(name, attr, request);
                    self.charts.push(chart);
                } else {
                    chart = charts[0];
                }
                return chart;
            },

            // remove the chart for name/attr
            // if all attrs are gone for this request, remove the request
            unRegisterChart: function (chart) {
                // remove the chart
                for (var i=0; i<self.charts.length; ++i) {
                    var c = self.charts[i];
                    if (chart.equals(c)) {
                        var request = chart.request();
                        self.delChart(chart);
                        if (request) {
                            // see if any other charts use this attr
                            for (var i=0; i<self.charts.length; ++i) {
                                var c = self.charts[i];
                                if (c.attr() == chart.attr() && c.request().equals(chart.request()))
                                    return;
                            }
                            // no other charts use this attr, so remove it
                            var attrIndex = request.attrs.indexOf(chart.attr());
                            request.attrs.splice(attrIndex, 1);
                            if (request.attrs.length == 0) {
                                self.stopCollecting(request);
                                self.delChartRequest(request);
                            }
                        }
                    }
                }
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
            // send the request
            sendChartRequest: function (request) {
                // ensure the response has the name field so we can associate the response values with the correct chart
                var attrs = request.attrs.slice(); // copy to temp array
                attrs.push("name");
                QDRService.getNodeInfo(request.nodeId, request.entity, attrs,
                    // this is called when the response is received
                    function (nodeId, entity, response) {
                        QDR.log.debug("got chart results for " + nodeId + " " + entity);
                        var records = response.results;
                        var now = new Date();
                        var nameIndex = response.attributeNames.indexOf("name");
                        if (nameIndex < 0)
                            return;
                        // for each record returned, find the charts for this request and save the data with this timestamp
                        for (var i=0; i<records.length; ++i) {
                            var name = records[i][nameIndex];
                            for (var j=0; j<self.charts.length; ++j) {
                                var chart = self.charts[j];
                                if (chart.name() == name && chart.request().equals(request)) {
                                    var attrIndex = response.attributeNames.indexOf(chart.attr());
                                    if (attrIndex > -1) {
                                        chart.data().push({date: now, value: records[i][attrIndex]});
                                        self.expireData(chart.data(), now, chart.duration());
                                    }
                                }
                            }
                        }
                        // it is now safe to send another request
                        request.setTimeoutHandle = setTimeout(self.sendChartRequest, request.interval, request)
                    });
            },

            expireData: function (data, now, duration) {
                var cutOff = new Date(now.getTime() - duration * 60 * 1000);
                while (data[0].date < cutOff) {
                    data.shift();
                }
            },

            numCharts: function () {
                return self.charts.length;
            },

            isAttrCharted: function (nodeId, entity, name, attr) {
                var charts = self.findCharts(name, attr, nodeId, entity);
                // if any of the matching charts are on the dashboard page, return true
                return charts.some(function (chart) {
                    return (chart.dashboard) });
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

                self.charts.each(function (chart) {
                    var minChart = {};
                    // don't save chart unless it is on the dashboard
                    if (chart.dashboard) {
                        chart.copyProps(minChart);
                        minCharts.push(minChart);
                    }
                })
                localStorage["QDRCharts"] = angular.toJson(minCharts);
            },
            loadCharts: function () {
                var charts = angular.fromJson(localStorage["QDRCharts"]);
                if (charts) {
                    charts.each(function (chart) {
                        if (!chart.interval)
                            chart.interval = 1000;
                        if (!chart.duration)
                            chart.duration = 10;
                        var newChart = self.registerChart(chart.nodeId, chart.entity, chart.name, chart.attr, chart.interval, true);
                        newChart.dashboard = true;  // we only save the dashboard charts
                        newChart.type = chart.type;
                        newChart.rateWindow = chart.rateWindow;
                        newChart.areaColor = chart.areaColor ? chart.areaColor : "#EEFFEE";
                        newChart.lineColor = chart.lineColor ? chart.lineColor : "#4682b4";
                        newChart.duration(chart.duration);
                        newChart.visibleDuration = chart.visibleDuration ? chart.visibleDuration : 10;
                        if (chart.userTitle)
                            newChart.title(chart.userTitle);
                    })
                }
            },

            AreaChart: function (chart, url) {
                if (!chart)
                    return;

                this.chart = chart; // reference to underlying chart
                this.tschart = null;
                if (url)
                    url = "/dispatch" + url;
                else
                    url = "";
                this.url = url;

                // callback function. called by tschart when binding data
                // in the method, the variable 'this' refers to the svg and not the AreaChart,
                // but since we are still in the scope of the AreaChart we have access to the passed in chart argument
                this.chartData = function () {

                    var now = new Date();
                    var visibleDate = new Date(now.getTime() - chart.visibleDuration * 60 * 1000);

                    if (chart.type == "rate") {
                        var rateData = [];
                        k = 0;  // inner loop optimization
                        for (var i=0; i<chart.data().length; ++i) {
                            var d = chart.data()[i];
                            if (d.date >= visibleDate)
                            {
                                for (var j=k+1; j<chart.data().length; ++j) {
                                    var d1 = chart.data()[j];
                                    if (d1.date - d.date >= chart.rateWindow) {
                                        rateData.push({date: d1.date, value: d1.value - d.value});
                                        k = j; // start here next time
                                        break;
                                    }
                                }
                            }
                        }
                        // we need at least a point to chart
                        if (rateData.length == 0) {
                            rateData[0] = {date: chart.data()[0].date, value: 0};
                        }
                        return rateData;
                    }
                    if (chart.visibleDuration != chart.duration()) {
                        return chart.data.filter(function (d) { return d>=visibleDate});
                    } else
                        return chart.data();
                }

                // redraw the chart
                this.tick = function (id) {

                    // can't draw charts that don't have data yet
                    if (this.chart.data().length == 0) {
                        return;
                    }

                    if (!this.tschart) {
                        var div = angular.element('#' + id);
                        if (!div)
                            return;

                        var width = div.width();
                        var height = div.height();

                        if (!width)
                            return;

                        // initialize the chart
                        this.tschart = self.timeSeriesChart()
                            .x(function(d) { return d.date; })
                            .y(function(d) { return d.value; })
                            .width(width)
                            .height(height)
                            .title(this.chart.title());
                    }

                    // in case the chart type has changed, set the new type
                    this.tschart.type(this.chart.type)
                        .areaColor(this.chart.areaColor)
                        .lineColor(this.chart.lineColor)
                        .url(this.url)
                        .title(this.chart.title());

                    // bind the new data and update the chart
                    d3.select('#' + id)         // the div id on the page/dialog
                        .datum(this.chartData)     // the callback function to get the data
                        .call(this.tschart);       // the charting function
                }
            },

            timeSeriesChart: function () {
                var margin = {top: 24, right: 46, bottom: 20, left: 20},
                    width = 760,
                    height = 120,
                    xValue = function(d) {return d[0];},
                    yValue = function(d) {return d[1];},
                    xScale = d3.time.scale(),
                    yScale = d3.scale.linear(),
                    xAxis = d3.svg.axis().scale(xScale).orient("bottom").ticks(d3.time.minutes, 2).tickSize(6, 0),
                    yAxis = d3.svg.axis().scale(yScale).orient("right").ticks(3).tickFormat(function(d) { return formatValue(d)}),
                    area = d3.svg.area().interpolate("basis").x(X).y1(Y),
                    line = d3.svg.line().x(X).y(Y),
                    title = "",
                    url = "",       // page url to prepend to url('#xxxx') selections
                    type = "value",
                    areaColor = "",
                    lineColor = "";

                var formatValue = d3.format(".2s");
                var formatPrecise = d3.format(",");
                var bisectDate = d3.bisector(function(d) { return d[0]; }).left;

                function chart(selection) {
                    selection.each(function(data) {

                        yAxis.tickSize(width - margin.left - margin.right);    // set here since width can be set after initialization

                        // Convert data to standard representation greedily;
                        // this is needed for nondeterministic accessors.
                        data = data.map(function(d, i) {
                            return [xValue.call(data, d, i), yValue.call(data, d, i)];
                        });

                        // Update the x-scale.
                        var extent = d3.extent(data, function(d) {return d[0];});
                        xScale
                          .domain(extent)
                          .range([0, width - margin.left - margin.right]);

                        // Update the y-scale.
                        var min = d3.min(data, function(d) {return d[1]});
                        var max = d3.max(data, function(d) {return d[1]});
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

                        yScale
                          .domain([min, max])
                          .range([height - margin.top - margin.bottom, 0]);

                        if (type == "rate") {
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
                        var minStr = formatValue(min);
                        var maxStr = formatValue(max);
                        if (minStr == maxStr)
                            yAxis.ticks(1);

                        // Select the svg element, if it exists.
                        var svg = d3.select(this).selectAll("svg").data([data]);

                        // Otherwise, create the skeletal chart.
                        // anything appended to svgEnter will be done when the svg is created and not
                        // every time it is updated
                        var svgEnter = svg.enter().append("svg");

                    /*<defs>
                        <filter id="dropshadow" height="130%">
                          <feGaussianBlur in="SourceAlpha" stdDeviation="3"/>
                          <feOffset dx="2" dy="2" result="offsetblur"/>
                          <feComponentTransfer>
                            <feFuncA type="linear" slope="0.2"/>
                          </feComponentTransfer>
                          <feMerge>
                            <feMergeNode/>
                            <feMergeNode in="SourceGraphic"/>
                          </feMerge>
                        </filter>
                      </defs>*/
                        var filter = svgEnter.append("svg:defs")
                                        .append("filter")
                                          .attr("id", "dropshadow")
                                          .attr("height", "130%");
                        filter.append("feGaussianBlur").attr("in", "SourceAlpha").attr("stdDeviation", 3);
                        filter.append("feOffset").attr("dx", "2").attr("dy", "2").attr("result", "offsetblur");
                        filter.append("feComponentTransfer")
                                .append("feFuncA").attr("type", "linear").attr("slope", "0.5");
                        var feMerge = filter.append("feMerge");
                        feMerge.append("feMergeNode")
                        feMerge.append("feMergeNode").attr("in", "SourceGraphic");


                        var gEnter = svgEnter.append("g");
                        gEnter.append("path").attr("class", "area");
                        gEnter.append("path").attr("class", "line");
                        gEnter.append("g").attr("class", "x axis");
                        gEnter.append("g").attr("class", "y axis");
                        gEnter.append("text").attr("class", "title")
                                .attr("x", (width / 2) - (margin.left + margin.right) / 2)
                                .attr("y", 0 - (margin.top / 2))
                                .attr("text-anchor", "middle")
                                .text(title);

                        // Update the outer dimensions.
                        svg.attr("width", width)
                          .attr("height", height);

                        // Update the inner dimensions.
                        var g = svg.select("g")
                          .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

                        g.select(".title").text(title);
                        // Update the area path.
                        var path = g.select(".area")
                          .attr("d", area.y0(yScale.range()[0]));
                        if (areaColor != "") {
                            path.style("fill", areaColor);  // assumed to be already validated
                        } // else the color from the style sheet will be used

                        //Update the line path.
                        var topLine = g.select(".line")
                          .attr("d", line);
                        if (lineColor != "")
                            topLine.style("stroke", lineColor);

                        // Update the x-axis.
                        g.select(".x.axis")
                          .attr("transform", "translate(0," + (yScale.range()[0] -1) + ")")
                          .call(xAxis);

                        g.select(".y.axis")
                          .attr("width", width)
                          .attr("transform", "translate(" + (xScale.range()[0]) + ",0)")
                          .call(yAxis);

                        var focus = svgEnter.append("g")
                          .attr("class", "focus")
                          .style("display", "none");

                        focus.append("circle")
                          .attr("r", 4.5);

                        var focusg = focus.append("g");
                        focusg.append("rect")
                            .attr("class", "mo-rect")   // mouseover rect
                            .attr("width", 60)
                            .attr("height", 18)
                            .attr("x", -60)
                            .attr("y", -20)
                            .attr("filter", "url(" + url + "#dropshadow)"); // use the current path
                            // see https://github.com/angular/angular.js/issues/8934

                        focusg.append("rect")
                            .attr("class", "mo-guide")
                            .attr("width", 1)
                            .attr("height", height - (margin.top + margin.bottom));
                        focusg.append("text")
                              .attr("x", 9) //-9)
                              .attr("dy", -7) //"-1em")
                              .attr("text-anchor", "start");

                        // prepend the url to the defs:filter at update time
                        //svg.select('.mo-rect').attr("filter", "url(" + url + "#dropshadow)");

                        focus = svg.select('.focus');

                        // TODO: fix this
                        // need to recreate this every update... not sure why
                        var overlay = svg.select(".overlay");
                        if (!overlay.empty())
                                overlay.remove();
                        svg.append("rect")
                          .attr("class", "overlay")
                          .attr("width", width)
                          .attr("height", height)
                          .on("mouseover", function() { focus.style("display", null); })
                          .on("mouseout", function() { focus.style("display", "none"); })
                          .on("mousemove", mousemove)

                        function mousemove() {
                            var x0 = xScale.invert(d3.mouse(this)[0] - margin.left);
                            var i = bisectDate(data, x0, 1);
                            if (i < data.length && i > 0) {
                                var d0 = data[i - 1];
                                var d1 = data[i];
                                var d = x0 - d0[0] > d1[0] - x0 ? d1 : d0;
                                focus.attr("transform", "translate(" + (xScale(d[0]) + margin.left) + "," + (yScale(d[1]) + margin.top) + ")");
                                var trect = focus.select(".mo-rect");
                                var guide = focus.select(".mo-guide");
                                guide.attr("y", -yScale(d[1]));
                                var text = focus.select("text").text(formatPrecise(d[1]));
                                var tnw = text.node().getBBox().width;
                                var mx = xScale(d[0]); // mouse x
                                trect.attr("width", tnw + 16);
                                var tx = text.attr("x");
                                // if the text is off the left side of the svg, align it on the right instead of left
                                if (mx - tnw < -9) {
                                    text.attr("x", 9);
                                }
                                // if text is off the right side of the svg
                                if (tnw + mx + margin.left + 9 > width) {
                                    text.attr("x", -tnw - 9);
                                }
                                var tx = text.attr("x");
                                trect.attr("x", tx - 8);

                            } else {
                                focus.attr("transform", "translate(-10,-10)");
                            }
                        }

                    });
                }

                // The x-accessor for the path generator; xScale ∘ xValue.
                function X(d) {
                    return xScale(d[0]);
                }

                // The x-accessor for the path generator; yScale ∘ yValue.
                function Y(d) {
                    return yScale(d[1]);
                }

                chart.margin = function(_) {
                    if (!arguments.length) return margin;
                    margin = _;
                    return chart;
                };

                chart.width = function(_) {
                    if (!arguments.length) return width;
                    width = _;
                    return chart;
                };

                chart.height = function(_) {
                    if (!arguments.length) return height;
                    height = _;
                    return chart;
                };

                chart.title = function (_) {
                    if (!arguments.length) return title;
                    title = _;
                    return chart;
                };

                chart.url = function (_) {
                    if (!arguments.length) return url;
                    url = _;
                    return chart;
                };

                chart.type = function (_) {
                    if (!arguments.length) return type;
                    type = _;
                    return chart;
                };

                chart.areaColor = function (_) {
                    if (!arguments.length) return areaColor;
                    areaColor = _;
                    return chart;
                };

                chart.lineColor = function (_) {
                    if (!arguments.length) return lineColor;
                    lineColor = _;
                    return chart;
                };

                chart.x = function(_) {
                    if (!arguments.length) return xValue;
                    xValue = _;
                    return chart;
                };

                chart.y = function(_) {
                    if (!arguments.length) return yValue;
                    yValue = _;
                    return chart;
                };

                return chart;
            },

        }
        return self;
  });

  return QDR;
}(QDR || {}));
