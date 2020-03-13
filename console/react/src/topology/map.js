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

import * as d3 from "d3";
import * as topojson from "topojson-client";

const maxnorth = 84;
const maxsouth = 74;
const MAPOPTIONSKEY = "QDRMapOptions";
const MAPPOSITIONKEY = "QDRMapPosition";
const defaultLandColor = "#A3D3E0";
const defaultOceanColor = "#FFFFFF";

export class BackgroundMap {
  // eslint-disable-line no-unused-vars
  constructor($scope, options, notifyFn) {
    this.$scope = $scope;
    this.initialized = false;
    this.notify = notifyFn;
    this.options = options;

    let savedOptions = localStorage.getItem(MAPOPTIONSKEY);
    this.mapOptions = savedOptions
      ? JSON.parse(savedOptions)
      : {
          areaColor: defaultLandColor,
          oceanColor: defaultOceanColor
        };
    this.last = {
      translate: [0, 0],
      scale: null
    };
  }
  updateMapColor(which, color) {
    if (which === "areaColor") {
      this.updateLandColor(color);
    } else if (which === "oceanColor") {
      this.updateOceanColor(color);
    }
    return this.mapOptions;
  }
  updateLandColor(color) {
    this.mapOptions.areaColor = color;
    localStorage[MAPOPTIONSKEY] = JSON.stringify(this.mapOptions);
    d3.select("g.geo path.land")
      .style("fill", color)
      .style("stroke", d3.rgb(color).darker());
  }
  updateOceanColor(color) {
    if (!color) color = this.mapOptions.oceanColor;
    this.mapOptions.oceanColor = color;
    localStorage[MAPOPTIONSKEY] = JSON.stringify(this.mapOptions);
    d3.select("g.geo rect.ocean").style("fill", color);
    this.setBackgroundColor();
  }

  setBackgroundColor() {
    const color = this.options.show ? this.mapOptions.oceanColor : "#FFF";
    d3.select(".pf-c-page__main").style("background-color", color);
  }

  setWidthHeight(width, height) {
    if (!this.initialized) return;
    this.width = width;
    this.height = height;
    // track last translation and scale event we processed
    this.rotate = 20;
    this.scaleExtent = [1, 10];

    // setup the projection with some defaults
    this.projection = d3.geo
      .mercator()
      .rotate([this.rotate, 0])
      .scale(1)
      .translate([width / 2, height / 2]);

    // this path will hold the land coordinates once they are loaded
    this.geoPath = d3.geo.path().projection(this.projection);

    // set up the scale extent and initial scale for the projection
    var b = getMapBounds(this.projection, Math.max(maxnorth, maxsouth)),
      s = width / (b[1][0] - b[0][0]);
    this.scaleExtent = [s, 15 * s];

    this.projection.scale(this.scaleExtent[0]);

    this.zoom = d3.behavior
      .zoom()
      .scaleExtent(this.scaleExtent)
      .scale(this.projection.scale())
      .translate([0, 0]) // not linked directly to projection
      .on("zoom", this.zoomed);

    this.geo
      .select("rect.ocean")
      .attr("width", width)
      .attr("height", height)
      .attr("fill", "#FFF");

    if (this.options.show) {
      this.svg.call(this.zoom).on("dblclick.zoom", null);
    }

    // restore map rotate, scale, translate
    this.restoreState();

    // draw with current positions
    this.geo.selectAll(".land").attr("d", this.geoPath);
  }

  setSvg(svg, width, height) {
    this.svg = svg;
    this.geo = svg
      .append("g")
      .attr("class", "geo")
      .style("opacity", this.options.show ? "1" : "0");

    this.geo
      .append("rect")
      .attr("class", "ocean")
      .attr("width", width)
      .attr("height", height)
      .attr("fill", "#FFF");

    if (this.options.show && this.zoom) {
      this.svg.call(this.zoom).on("dblclick.zoom", null);
    }
  }

  init(_unused, svg, width, height) {
    return new Promise((resolve, reject) => {
      if (this.initialized) {
        resolve();
        return;
      }
      this.width = width;
      this.height = height;
      // track last translation and scale event we processed
      this.rotate = 20;
      this.scaleExtent = [1, 10];

      // setup the projection with some defaults
      this.projection = d3.geo
        .mercator()
        .rotate([this.rotate, 0])
        .scale(1)
        .translate([width / 2, height / 2]);

      // this path will hold the land coordinates once they are loaded
      this.geoPath = d3.geo.path().projection(this.projection);

      // set up the scale extent and initial scale for the projection
      var b = getMapBounds(this.projection, Math.max(maxnorth, maxsouth)),
        s = width / (b[1][0] - b[0][0]);
      this.scaleExtent = [s, 15 * s];

      this.projection.scale(this.scaleExtent[0]);

      let savedOptions = localStorage.getItem(MAPPOSITIONKEY);
      this.lastProjection = savedOptions
        ? JSON.parse(savedOptions)
        : {
            rotate: -10.884378033730373,
            scale: this.scaleExtent[0],
            translate: [width / 2, height / 2]
          };

      this.zoom = d3.behavior
        .zoom()
        .scaleExtent(this.scaleExtent)
        .scale(this.projection.scale())
        .translate([0, 0]) // not linked directly to projection
        .on("zoom", this.zoomed);

      if (!this.svg) this.setSvg(svg, width, height);

      fetch("data/countries.json")
        .then(res => res.json())
        .then(world => {
          this.geo
            .append("path")
            .datum(topojson.feature(world, world.objects.countries))
            .attr("class", "land")
            .attr("d", this.geoPath)
            .style("stroke", d3.rgb(this.mapOptions.areaColor).darker());

          this.updateLandColor(this.mapOptions.areaColor);
          this.updateOceanColor(this.mapOptions.oceanColor);

          // restore map rotate, scale, translate
          this.restoreState();

          // draw with current positions
          this.geo.selectAll(".land").attr("d", this.geoPath);

          this.initialized = true;
          resolve();
        })
        .catch(error => {
          reject(error);
        });
    });
  }

  setMapOpacity(opacity) {
    opacity = opacity ? 1 : 0;
    if (this.width && this.width < 768) opacity = 0;
    if (this.geo) this.geo.style("opacity", opacity);
  }
  restoreState() {
    this.projection.rotate([this.lastProjection.rotate, 0]);
    this.projection.translate(this.lastProjection.translate);
    this.projection.scale(this.lastProjection.scale);
    this.zoom.scale(this.lastProjection.scale);
    this.zoom.translate(this.lastProjection.translate);
  }

  // stop responding to pan/zoom events
  cancelZoom() {
    this.saveProjection();
  }

  // tell the svg to respond to mouse pan/zoom events
  restartZoom() {
    this.svg.call(this.zoom).on("dblclick.zoom", null);
    this.restoreState();
    this.last.scale = null;
  }

  getXY(lon, lat) {
    return this.projection([lon, lat]);
  }
  getLonLat(x, y) {
    return this.projection.invert([x, y]);
  }

  zoomed = () => {
    if (
      d3.event &&
      !this.$scope.current_node &&
      !this.$scope.mousedown_node &&
      this.options.show
    ) {
      let scale = d3.event.scale,
        t = d3.event.translate,
        dx = t[0] - this.last.translate[0],
        dy = t[1] - this.last.translate[1],
        yaw = this.projection.rotate()[0],
        tp = this.projection.translate();
      // zoomed
      if (scale !== this.last.scale) {
        // get the mouse's x,y relative to the svg
        let top = d3.select(".pf-c-page__main").node().offsetTop;
        let left = d3.select(".pf-c-page__main").node().offsetLeft;
        let mx = d3.event.sourceEvent.clientX - left;
        let my = d3.event.sourceEvent.clientY - top - 1;

        // get the lon,lat at the mouse position
        let lonlat = this.projection.invert([mx, my]);

        // do the requested scale operation
        this.projection.scale(scale);

        // get the lonlat that is under the mouse after the scale
        let lonlat1 = this.projection.invert([mx, my]);
        // calc the distance to rotate based on change in longitude
        dx = lonlat1[0] - lonlat[0];
        // calc the distance to translate based on change in lattitude
        dy = my - this.projection([0, lonlat[1]])[1];

        // rotate the map so that the longitude under the mouse is where it was before the scale
        this.projection.rotate([yaw + dx, 0, 0]);

        // translate the map so that the lattitude under the mouse is where it was before the scale
        this.projection.translate([tp[0], tp[1] + dy]);
      } else {
        // rotate instead of translate in the x direction
        this.projection.rotate([
          yaw + (((360.0 * dx) / this.width) * this.scaleExtent[0]) / scale,
          0,
          0
        ]);
        // translate only in the y direction. don't translate beyond the max lattitude north or south
        var bnorth = getMapBounds(this.projection, maxnorth),
          bsouth = getMapBounds(this.projection, maxsouth);
        if (bnorth[0][1] + dy > 0) dy = -bnorth[0][1];
        else if (bsouth[1][1] + dy < this.height) dy = this.height - bsouth[1][1];
        this.projection.translate([tp[0], tp[1] + dy]);
      }
      this.last.scale = scale;
      this.last.translate = t;
      this.saveProjection();
      this.notify();
    }
    // update the land path with our current projection
    this.geo.selectAll(".land").attr("d", this.geoPath);
  };
  saveProjection() {
    if (this.projection) {
      this.lastProjection.rotate = this.projection.rotate()[0];
      this.lastProjection.scale = this.projection.scale();
      this.lastProjection.translate = this.projection.translate();
      localStorage[MAPPOSITIONKEY] = JSON.stringify(this.lastProjection);
    }
  }
}

// find the top left and bottom right of current projection
function getMapBounds(projection, maxlat) {
  var yaw = projection.rotate()[0],
    xymax = projection([-yaw + 180 - 1e-6, -maxlat]),
    xymin = projection([-yaw - 180 + 1e-6, maxlat]);

  return [xymin, xymax];
}
